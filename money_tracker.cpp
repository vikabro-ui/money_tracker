#include <exception>
#include <iostream>
#include <string>
#include <cmath>
#include <pqxx/pqxx>
#include <httplib.h> 
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DatabaseManager 
{
private:
    std::string connection_string_;

public:
    DatabaseManager(const std::string& dbname, const std::string& user, 
                    const std::string& password, const std::string& host = "127.0.0.1", int port = 5432) 
    {
        connection_string_ = "dbname=" + dbname +
                             " user=" + user +
                             " password=" + password +
                             " host=" + host +
                             " port=" + std::to_string(port);
    }

    bool testConnection() 
    {
        try {
            pqxx::connection C(connection_string_);
            if (C.is_open()) {
                std::cout << "[УСПЕХ] Успешное подключение к базе данных: " << C.dbname() << "\n";
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА] Ошибка подключения: " << e.what() << "\n";
        }
        return false;
    }

    bool addAccount(const std::string& name, double balance, const std::string& currency = "BYN") 
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);

            W.exec_params(
                "INSERT INTO accounts (name, balance, currency) VALUES ($1, $2, $3);",
                name, balance, currency
            );

            W.commit();
            std::cout << "[УСПЕХ] Счет \"" << name << "\" успешно добавлен!\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА] Не удалось добавить счет: " << e.what() << "\n";
            return false;
        }
    }

    bool addTransaction(int account_id, int category_id, double amount, const std::string& description) 
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);

            W.exec_params(
                "INSERT INTO transactions (account_id, category_id, amount, description) VALUES ($1, $2, $3, $4);",
                account_id, category_id, amount, description
            );

            W.exec_params(
                "UPDATE accounts SET balance = balance + $1 WHERE id = $2;",
                amount, account_id
            );
            
           if (amount < 0) {
                pqxx::result budget_res = W.exec_params(
                    "SELECT monthly_limit FROM budgets WHERE category_id = $1;",
                    category_id
                );

                if (!budget_res.empty()) {
                    double monthly_limit = budget_res[0]["monthly_limit"].as<double>();
                        
                    pqxx::result spent_res = W.exec_params(
                        "SELECT SUM(amount) FROM transactions "
                        "WHERE category_id = $1 AND amount < 0 "
                        "AND date >= date_trunc('month', CURRENT_DATE);",
                        category_id
                    );

                    double total_spent = std::abs(spent_res[0][0].as<double>());

                    std::cout << "[АНАЛИТИКА] Потрачено в этом месяце по категории: " << total_spent 
                    << " из лимита " << monthly_limit << "\n";

                    if (total_spent > monthly_limit) {
                        std::cout << " [ПРЕДУПРЕЖДЕНИЕ] Вы превысили месячный лимит по этой категории на " 
                        << (total_spent - monthly_limit) << "!\n";
                    }
                }
            }

            W.commit();
            std::cout << "[УСПЕХ] Транзакция на сумму " << amount << " успешно добавлена!\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА] Не удалось добавить транзакцию: " << e.what() << "\n";
            return false;
        }
    }

    void handleBankWebhook(const httplib::Request& req, httplib::Response& res) 
{ 
    try {
        std::cout << "\n[СИГНАЛ] Получен вебхук от банка!\n";

        json bank_data = json::parse(req.body);
        int account_id = bank_data["account_id"];
        int category_id = bank_data["category_id"];
        double amount = bank_data["amount"];
        std::string description = bank_data["description"];

        bool success = addTransaction(account_id, category_id, amount, description);
        
        if (success) {
           
            const int SALARY_CATEGORY_ID = 6; 

            if (amount > 0 && category_id == SALARY_CATEGORY_ID) {
                std::cout << "[ЗАЧИСЛЕНИЕ СРЕДСТВ] Зафиксировано поступление зарплаты (" << amount << " BYN). Пересчитываем лимиты...\n";
                distributeIncomeAndSetBudgets(amount);
            }

            res.set_content("{\"status\": \"ok\", \"message\": \"Transaction processed\"}", "application/json");
        } else {
            res.status = 500;
            res.set_content("{\"status\": \"error\", \"message\": \"Database error\"}", "application/json");
        }

    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА ПАРСИНГА JSON] " << e.what() << "\n";
        res.status = 400;
        res.set_content("{\"status\": \"error\", \"message\": \"Invalid JSON\"}", "application/json");
    }
}

    std::string getBalancesJson() 
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);

            pqxx::result res = W.exec("SELECT id, name, balance, currency FROM accounts;");

            json accounts_array = json::array();

            for (auto row : res) {
                json acc;
                acc["id"] = row["id"].as<int>();
                acc["name"] = row["name"].as<std::string>();
                acc["balance"] = row["balance"].as<double>();
                acc["currency"] = row["currency"].as<std::string>();
                accounts_array.push_back(acc);
            }

            json response;
            response["status"] = "ok";
            response["accounts"] = accounts_array;

            return response.dump(); 
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА БД] Не удалось получить балансы: " << e.what() << "\n";
            return "{\"status\": \"error\", \"message\": \"Database error\"}";
        }
    }

    bool distributeIncomeAndSetBudgets(double total_income)
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);

            pqxx::result categories_res = W.exec("SELECT id, name, target_percentage FROM categories;");
            if (categories_res.empty()) {
                std::cout << "[ПРЕДУПРЕЖДЕНИЕ] Нет категорий для распределения бюджета!\n";
                return false;
            }

            double total_percentage = 0.0;
            for (auto row : categories_res) {
                double percentage = row["target_percentage"].as<double>();
                if (percentage > 0) {
                    total_percentage += percentage;
                }
            }

            if (total_percentage <= 0) {
                std::cout << "[ОШИБКА] Сумма процентов равна 0! Нечего распределять.\n";
                return false;
            }

            std::cout << "\n[ПЕРЕРАСПРЕДЕЛЕНИЕ] Общая сумма доходов: " << total_income << " BYN\n";
            std::cout << "[ИНФО] Сумма процентов в базе: " << total_percentage << "%\n";

           bool need_normalization = (std::abs(total_percentage - 100.0) > 0.01);
            if (need_normalization) {
                std::cout << "[АВТО-ПИЛОТ] Сумма не равна 100%. Пропорционально подстраиваем под 100%...\n";
            }

            for (auto row : categories_res) {
                int cat_id = row["id"].as<int>();
                std::string cat_name = row["name"].as<std::string>();
                double percentage = row["target_percentage"].as<double>();

                if (percentage <= 0) continue;

                
                double effective_percentage=need_normalization?(percentage / total_percentage) * 100.0 
                : percentage;

                double calculated_limit = total_income * (effective_percentage / 100.0);

                
                W.exec_params(
                    "UPDATE budgets SET monthly_limit = $1 WHERE category_id = $2;",
                    calculated_limit, cat_id
                );

                std::cout << "  - Категория \"" << cat_name << "\" (установлено: " << percentage 
                          << "%, скорректировано: " << effective_percentage << "%): лимит -> " 
                          << calculated_limit << " BYN\n";
            }

            W.commit();
            std::cout << "[УСПЕХ] Лимиты успешно пересчитаны с авто-регулировкой!\n";
            return true;

        } catch (std::exception& e) {
            std::cerr << "[ОШИБКА БД] Не удалось перераспределить лимиты: " << e.what() << "\n";
            return false;
        }
    }

    std::string getTransactionsJson() 
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);
            pqxx::result res = W.exec(
                "SELECT t.id, a.name as account_name, c.name as category_name, t.amount, t.description, t.date "
                "FROM transactions t "
                "JOIN accounts a ON t.account_id = a.id "
                "JOIN categories c ON t.category_id = c.id "
                "WHERE t.date >= date_trunc('month', CURRENT_DATE) "
                "ORDER BY t.date DESC;"
            );

            json tx_array = json::array();
            for (auto row : res) {
                json tx;
                tx["id"] = row["id"].as<int>();
                tx["account"] = row["account_name"].as<std::string>();
                tx["category"] = row["category_name"].as<std::string>();
                tx["amount"] = row["amount"].as<double>();
                tx["description"] = row["description"].as<std::string>();
                tx["date"] = row["date"].as<std::string>();
                tx_array.push_back(tx);
            }

            json response;
            response["status"] = "ok";
            response["transactions"] = tx_array;

            return response.dump();
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА БД] Не удалось получить транзакции: " << e.what() << "\n";
            return "{\"status\": \"error\", \"message\": \"Database error\"}";
        }
    }

    bool addCategoryFull(const std::string& name, const std::string& type, double target_percentage) 
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);

            W.exec_params(
                "INSERT INTO categories (name, type, target_percentage) VALUES ($1, $2, $3);",
                name, type, target_percentage
            );
             if (type == "expense") {
                W.exec(
                    "INSERT INTO budgets (category_id, monthly_limit) "
                    "SELECT id, 0.0 FROM categories WHERE id NOT IN (SELECT category_id FROM budgets);"
                );
            }

            W.commit();
            std::cout << "[УСПЕХ] Категория \"" << name << "\" (" << target_percentage << "%) успешно добавлена!\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА] Не удалось добавить категорию: " << e.what() << "\n";
            return false;
        }
    }

    void handleAddCategory(const httplib::Request& req, httplib::Response& res) 
    {
        try {
            json data = json::parse(req.body);
            std::string name = data["name"];
            std::string type = data["type"]; 
            double target_percentage = data.contains("target_percentage") ? data["target_percentage"].get<double>() : 0.0;

            bool success = addCategoryFull(name, type, target_percentage);

            if (success) {
                res.set_content("{\"status\": \"ok\", \"message\": \"Category added successfully\"}", "application/json");
            } else {
                res.status = 500;
                res.set_content("{\"status\": \"error\", \"message\": \"Database error\"}", "application/json");
            }
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА ПАРСИНГА JSON] " << e.what() << "\n";
            res.status = 400;
            res.set_content("{\"status\": \"error\", \"message\": \"Invalid JSON\"}", "application/json");
        }
    }
};

int main() {
    DatabaseManager db("money_tracker", "postgres", "12082008");

    if (!db.testConnection()) {
        std::cerr << "Не удалось запустить сервер: нет связи с базой данных.\n";
        return 1;
    }

    httplib::Server svr;

    svr.Post("/bank-webhook", [&db](const httplib::Request& req, httplib::Response& res) {
        db.handleBankWebhook(req, res);
    });

    svr.Get("/get-balances", [&db](const httplib::Request& req, httplib::Response& res) {
        std::cout << "\n[ЗАПРОС] Получен запрос на просмотр баланса счетов\n";
        
        std::string json_response = db.getBalancesJson();
        res.set_content(json_response, "application/json");
    });

    svr.Get("/get-transactions", [&db](const httplib::Request& req, httplib::Response& res) {
        std::cout << "\n[ЗАПРОС] Получена история транзакций\n";
        std::string json_response = db.getTransactionsJson();
        res.set_content(json_response, "application/json");
    });


    svr.Post("/add-category", [&db](const httplib::Request& req, httplib::Response& res) {
        std::cout << "\n[ЗАПРОС] Добавление новой категории\n";
        db.handleAddCategory(req, res);
    });

    std::cout << "[СЕРВЕР ЗАПУЩЕН] Ожидание банковских вебхуков на http://127.0.0.1:8080/bank-webhook ...\n";
    svr.listen("0.0.0.0", 8080);

    return 0;
}
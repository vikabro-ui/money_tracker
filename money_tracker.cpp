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

    bool addCategory(const std::string& name, const std::string& type) 
    {
        try {
            pqxx::connection C(connection_string_);
            pqxx::work W(C);

            W.exec_params(
                "INSERT INTO categories (name, type) VALUES ($1, $2);",
                name, type
            );

            W.commit();
            std::cout << "[УСПЕХ] Категория \"" << name << "\" успешно добавлена!\n";
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[ОШИБКА] Не удалось добавить категорию: " << e.what() << "\n";
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
        try{
        std::cout << "\n[СИГНАЛ] Получен вебхук от банка!\n";

        json bank_data = json::parse(req.body);
            int account_id = bank_data["account_id"];
            int category_id = bank_data["category_id"];
            double amount = bank_data["amount"];
            std::string description = bank_data["description"];

            bool success = addTransaction(account_id, category_id, amount, description);

            if (success) {
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
};

int main() {
    DatabaseManager db("money_tracker", "postgres", "12082008");

    if (!db.testConnection()) {
        std::cerr << "Не удалось запустить сервер: нет связи с базой данных.\n";
        return 1;
    }

    httplib::Server svr;

    // Теперь здесь чистый вызов нашей функции-обработчика через лямбду
    svr.Post("/bank-webhook", [&db](const httplib::Request& req, httplib::Response& res) {
        db.handleBankWebhook(req, res);
    });

    std::cout << "[СЕРВЕР ЗАПУЩЕН] Ожидание банковских вебхуков на http://127.0.0.1:8080/bank-webhook ...\n";
    svr.listen("0.0.0.0", 8080);

    return 0;
}
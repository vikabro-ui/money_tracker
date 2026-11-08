# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-src"
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-build"
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix"
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix/tmp"
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix/src/cpp-httplib-populate-stamp"
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix/src"
  "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix/src/cpp-httplib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix/src/cpp-httplib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/vikabro/back/project2/build/_deps/cpp-httplib-subbuild/cpp-httplib-populate-prefix/src/cpp-httplib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()

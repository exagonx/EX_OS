/* =============================================================================
 * tools/prove/connprova.cpp — MariaDB Connector/C++, inside EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     connprovapp <host> <port>
 *
 * A C++ program against libmariadbcpp.a (tools/mariadb-connector-cpp-exos)
 * over libmariadbclient.a: sql::Driver, a PreparedStatement in a
 * transaction, a ResultSet, an sql::SQLException from a bad query. One line
 * per check: CONNCPP ok <what> / CONNCPP NO <what>, then
 * "CONNCPP fine <failures>". Built by tools/prova_mariadb.sh:
 *   i386-exos-g++ -O2 connprova.cpp -o connprovapp -I.../include/mariadb
 *       -lmariadbcpp -lmariadbclient -lssl -lcrypto -lz -lzstd -lpthread
 * ============================================================================= */
#include <conncpp.hpp>
#include <cstdio>
#include <memory>
#include <string>

static int g_no;

static void esito(bool ok, const char *cosa, const std::string &perche = "")
{
    if (ok) std::printf("CONNCPP ok %s\n", cosa);
    else { std::printf("CONNCPP NO %s %s\n", cosa, perche.c_str()); g_no++; }
}

int main(int argc, char **argv)
{
    std::string url = std::string("jdbc:mariadb://") + (argc > 1 ? argv[1] : "127.0.0.1")
                      + ":" + (argc > 2 ? argv[2] : "3306") + "/prova";
    try {
        sql::Driver *drv = sql::mariadb::get_driver_instance();
        sql::Properties p({{"user", "root"}, {"password", ""}});
        std::unique_ptr<sql::Connection> c(drv->connect(url, p));
        esito(c && c->isValid(), "connessione");
        std::unique_ptr<sql::Statement> s(c->createStatement());
        s->execute("create table if not exists cpp (id int primary key, nome varchar(40)) engine=innodb");
        c->setAutoCommit(false);
        std::unique_ptr<sql::PreparedStatement> ps(c->prepareStatement("insert into cpp values (?, ?)"));
        for (int i = 1; i <= 3; i++) {
            ps->setInt(1, i);
            ps->setString(2, "CPP" + std::to_string(i));
            ps->executeUpdate();
        }
        c->commit();
        std::unique_ptr<sql::ResultSet> r(s->executeQuery("select id, nome from cpp order by id"));
        std::string tutte;
        while (r->next())
            tutte += (tutte.empty() ? "" : ",") + std::string(r->getString(2).c_str());
        std::printf("CONNCPP righe %s\n", tutte.c_str());
        esito(tutte == "CPP1,CPP2,CPP3", "righe lette");
        try {
            s->executeQuery("select * from tabella_che_non_esiste");
            esito(false, "eccezione da una query sbagliata");
        } catch (sql::SQLException &e) {
            esito(e.getErrorCode() == 1146, "eccezione da una query sbagliata", e.what());
        }
        c->close();
    } catch (sql::SQLException &e) {
        esito(false, "eccezione", e.what());
    }
    std::printf("CONNCPP fine %d\n", g_no);
    return g_no != 0;
}

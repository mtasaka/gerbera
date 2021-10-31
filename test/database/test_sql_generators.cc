/*GRB*

Gerbera - https://gerbera.io/

    test_database.cc - this file is part of Gerbera.

    Copyright (C) 2021 Gerbera Contributors

    Gerbera is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.

    Gerbera is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Gerbera.  If not, see <http://www.gnu.org/licenses/>.
*/

/// \file test_sql_generators.cc
#include <fmt/core.h>
#include <gtest/gtest.h>

#include "database/sql_database.h"

class TestDatabase : public SQLDatabase, public std::enable_shared_from_this<SQLDatabase> {
public:
    using SQLDatabase::identifier;

    TestDatabase(std::shared_ptr<Config> config, std::shared_ptr<Mime> mime)
        : SQLDatabase(config, mime)
    {
        table_quote_begin = '[';
        table_quote_end = ']';
    }

    void storeInternalSetting(const std::string& key, const std::string& value) override { }

    bool threadCleanupRequired() const override { return false; }

    void threadCleanup() override { }

    std::shared_ptr<Database> getSelf() override { return this->shared_from_this(); }

    void shutdownDriver() override { }

    std::string quote(const std::string& str) const override
    {
        return fmt::format("\"{}\"", str);
    }

    int exec(const std::string& query, bool) override
    {
        lastStatement = query;
        return 0;
    }

    void _exec(const std::string& query) override
    {
        lastStatement = query;
    }

    std::shared_ptr<SQLResult> select(const std::string& query) override
    {
        lastStatement = query;
        return {};
    }

    std::string lastStatement;
};

class DatabaseTest : public ::testing::Test {
public:
    void SetUp() override
    {
        database = std::make_shared<TestDatabase>(config, mime);
    }

    void TearDown() override
    {
        database = {};
    }

protected:
    std::shared_ptr<Config> config;
    std::shared_ptr<Mime> mime;
    std::shared_ptr<TestDatabase> database;
};

TEST_F(DatabaseTest, BasicFormattingTest)
{
    EXPECT_EQ(database->quote("123"), "\"123\"");
    EXPECT_EQ(fmt::to_string(database->identifier("id")), "[id]");
}

TEST_F(DatabaseTest, UpdateTest)
{
    database->updateRow(
        "Table",
        { ColumnUpdate(database->identifier("a"), "4"), ColumnUpdate(database->identifier("b"), "101") },
        "id", 1234);
    EXPECT_EQ(database->lastStatement, "UPDATE [Table] SET [a] = 4, [b] = 101 WHERE [id] = 1234");
}

TEST_F(DatabaseTest, DeleteTest)
{
    database->deleteAll("Table");
    EXPECT_EQ(database->lastStatement, "DELETE FROM [Table]");

    database->deleteRow("Table", "id", 123);
    EXPECT_EQ(database->lastStatement, "DELETE FROM [Table] WHERE [id] = 123");

    database->deleteRow("Table", "id", std::string("Text"));
    EXPECT_EQ(database->lastStatement, "DELETE FROM [Table] WHERE [id] = \"Text\"");

    database->deleteRows("Table", "id", { 1, 2, 3 });
    EXPECT_EQ(database->lastStatement, "DELETE FROM [Table] WHERE [id] IN (1,2,3)");
}
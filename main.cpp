#include <iostream>
#include <sql.h>
#include <sqlext.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <string>
#include "duckdb/common/adbc/adbc.hpp"


const int numRuns = 5;

void check_ret(SQLRETURN ret, std::string msg) {
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        std::cout << ret << ": " << msg << " failed" << std::endl;
        exit(1);
    }
    if (ret == SQL_SUCCESS_WITH_INFO) {
        std::cout << ret << ": " << msg << " succeeded with info" << std::endl;
    }
}

void printError(SQLHDBC hdbc, SQLHSTMT hstmt) {
    SQLCHAR sqlState[SQL_SQLSTATE_SIZE + 1];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH + 1];
    SQLSMALLINT sqlStateLen, messageLen;

    SQLRETURN ret;
    SQLINTEGER i = 1;
    while ((ret = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, i, sqlState, NULL, message, SQL_MAX_MESSAGE_LENGTH + 1, &messageLen)) != SQL_NO_DATA) {
        SQLGetDiagField(SQL_HANDLE_STMT, hstmt, i, SQL_DIAG_SQLSTATE, sqlState, SQL_SQLSTATE_SIZE + 1, &sqlStateLen);
        std::cerr << "SQL State: " << sqlState << ", Error Message: " << message << std::endl;
        i++;
    }
}

void odbc(){
    SQLHANDLE env;
    SQLHANDLE dbc;
    SQLRETURN ret;
    
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    check_ret(ret, "SQLAllocHandle(env)");
    
    ret = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0);
    check_ret(ret, "SQLSetEnvAttr");
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    check_ret(ret, "SQLAllocHandle(dbc)");
    
    std::string dsn = "DSN=duckdbmemory";
    ret = SQLConnect(dbc, (SQLCHAR*)dsn.c_str(), SQL_NTS, NULL, 0, NULL, 0);
    check_ret(ret, "SQLConnect");
    
    std::cout << "Connected!" << std::endl;
    {
        SQLHSTMT hstmt;
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);

        SQLCHAR query[] = "CALL DBGEN(sf=1)";
        ret = SQLExecDirect(hstmt, query, SQL_NTS);
    }
    std::vector<double> executionTimes;

    for (int i = 0; i < numRuns; ++i) {
        auto startTime = std::chrono::high_resolution_clock::now();

        SQLHSTMT hstmt;
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &hstmt);

        // Prepare and execute the query
        SQLCHAR query[] = "SELECT * FROM lineitem;";
        ret = SQLExecDirect(hstmt, query, SQL_NTS);
        if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
            std::cerr << "Error executing the query." << std::endl;
            printError(dbc, hstmt);
            return;
        }

        // Get the number of columns in the result set
        SQLSMALLINT numColumns;
        SQLNumResultCols(hstmt, &numColumns);

           // Get column information
        std::vector<SQLSMALLINT> columnTypes(numColumns);
        for (int col = 0; col < numColumns; ++col) {
            SQLCHAR colName[256];
            SQLSMALLINT colNameLen;
            SQLDescribeCol(hstmt, col + 1, colName, 256, &colNameLen, &columnTypes[col], NULL, NULL, NULL);
        }

        while (SQLFetch(hstmt) == SQL_SUCCESS) {
        // Loop through each column and retrieve data with appropriate types
            for (int col = 1; col <= numColumns; ++col) {
                SQLLEN dataLen;
                if (columnTypes[col - 1] == SQL_INTEGER) {
                    SQLINTEGER intValue;
                    SQLGetData(hstmt, col, SQL_C_LONG, &intValue, 0, &dataLen);
                    // Process the integer value
                    // std::cout << intValue << ", ";
                } else if (columnTypes[col - 1] == SQL_REAL) {
                    SQLREAL floatValue;
                    SQLGetData(hstmt, col, SQL_C_FLOAT, &floatValue, 0, &dataLen);
                    // Process the floating-point value
                    // std::cout << floatValue << ", ";
                } else if (columnTypes[col - 1] == SQL_DECIMAL || columnTypes[col - 1] == SQL_NUMERIC) {
                    SQLCHAR decimalValue[128];
                    SQLGetData(hstmt, col, SQL_C_CHAR, decimalValue, sizeof(decimalValue), &dataLen);
                    // Process the decimal value (convert to double or string as needed)
                    // std::cout << std::string(decimalValue, dataLen) << ", ";
                } else if (columnTypes[col - 1] == SQL_TYPE_DATE) {
                    SQL_DATE_STRUCT dateValue;
                    SQLGetData(hstmt, col, SQL_C_DATE, &dateValue, 0, &dataLen);
                    // Process the date value
                    // std::cout << dateValue.year << "-" << dateValue.month << "-" << dateValue.day << ", ";
                } else {
                    SQLCHAR stringValue[256];
                    SQLGetData(hstmt, col, SQL_C_CHAR, stringValue, sizeof(stringValue), &dataLen);
                    // Process the string value
                    // std::cout << stringValue << ", ";
                }
            }
            // Move to the next row
            // std::cout << std::endl;
        }
        
        ret = SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        check_ret(ret, "SQLFreeHandle(hstmt)");
        auto endTime = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        executionTimes.push_back(static_cast<double>(duration));
    }
    std::sort(executionTimes.begin(), executionTimes.end());

    std::cout << "[ODBC] Median execution time: " << executionTimes[2]  << std::endl;

    
    ret = SQLDisconnect(dbc);
    check_ret(ret, "SQLDisconnect");
    
    ret = SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    check_ret(ret, "SQLFreeHandle(dbc)");
    
    ret = SQLFreeHandle(SQL_HANDLE_ENV, env);
    check_ret(ret, "SQLFreeHandle(env)");
}

void adbc(){
    duckdb_adbc::AdbcError adbc_error;
    duckdb_adbc::InitiliazeADBCError(&adbc_error);

    duckdb_adbc::AdbcDatabase adbc_database;
    duckdb_adbc::AdbcConnection adbc_connection;
    duckdb_adbc::AdbcStatement adbc_statement;
    ArrowArrayStream arrow_stream;
    ArrowArray arrow_array;
    auto duckdb_lib = "/Users/holanda/Documents/Projects/connector_benchmark/duckdb/build/release/src/libduckdb.dylib";
    int64_t rows_affected;

    AdbcDatabaseNew(&adbc_database, &adbc_error);
    AdbcDatabaseSetOption(&adbc_database, "driver", duckdb_lib, &adbc_error);
    AdbcDatabaseSetOption(&adbc_database, "entrypoint", "duckdb_adbc_init", &adbc_error);
    AdbcDatabaseSetOption(&adbc_database, "path", ":memory:", &adbc_error);

    AdbcDatabaseInit(&adbc_database, &adbc_error);

    AdbcConnectionNew(&adbc_connection, &adbc_error);
    AdbcConnectionInit(&adbc_connection, &adbc_database, &adbc_error);

    AdbcStatementNew(&adbc_connection, &adbc_statement, &adbc_error);
    AdbcStatementSetSqlQuery(&adbc_statement, "CALL DBGEN(sf=1)", &adbc_error);
    AdbcStatementExecuteQuery(&adbc_statement, &arrow_stream, &rows_affected, &adbc_error);
    std::vector<double> executionTimes;
    for (int i = 0; i < numRuns; ++i) {
        duckdb_adbc::AdbcStatement adbc_statement_q;
        auto startTime = std::chrono::high_resolution_clock::now();

        AdbcStatementNew(&adbc_connection, &adbc_statement_q, &adbc_error);
        AdbcStatementSetSqlQuery(&adbc_statement_q, "SELECT * FROM lineitem;", &adbc_error);
        AdbcStatementExecuteQuery(&adbc_statement_q, &arrow_stream, &rows_affected, &adbc_error);
        ArrowArray out;
        arrow_stream.get_next(&arrow_stream,&out);
        while (out.release){
           // std::cout << out.length << std::endl;
            out.release(&out);
            arrow_stream.get_next(&arrow_stream,&out);
        }
        if (out.release){
             out.release(&out);

        }
        auto endTime = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        executionTimes.push_back(static_cast<double>(duration));
        arrow_stream.release(&arrow_stream);
        AdbcStatementRelease(&adbc_statement_q, &adbc_error);

    }
    std::sort(executionTimes.begin(), executionTimes.end());
    std::cout << "[ADBC] Median execution time: " << executionTimes[2]  << std::endl;

}

int main() {
    odbc();
    adbc();
}
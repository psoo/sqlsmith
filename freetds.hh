#ifndef SQLSMITH_FREETDS_HH
#define SQLSMITH_FREETDS_HH

#include <string>

#include <sybfront.h>
#include <sybdb.h>
#include <syberror.h>

#include "schema.hh"

struct freetds_msginfo_base {

  DBPROCESS *dbproc = NULL;
  int severity;

  freetds_msginfo_base() {
    dbproc = NULL;
    severity = 0;
  };

  freetds_msginfo_base(DBPROCESS *dbproc, int severity)  {
    this->dbproc = dbproc;
    this->severity = severity;
  }

  virtual std::string msg() = 0;

};

struct freetds_err : freetds_msginfo_base {

  int dberr;
  int oserr;
  std::string oserrstr;
  std::string dberrstr;

  freetds_err(DBPROCESS *dbproc,
              int severity,
              int dberr,
              int oserr,
              std::string
              dberrstr,
              std::string oserrstr);

  virtual std::string msg();

};

struct freetds_msg : freetds_msginfo_base {

  DBINT msgno;
  int msgstate;
  std::string msgtext;
  std::string ident;
  std::string progname;
  int line;

  freetds_msg(DBPROCESS *dbproc,
              int severity,
              DBINT msgno,
              int msgstate,
              std::string msgtext,
              std::string ident,
              std::string progname,
              int line);

  virtual std::string msg();
  virtual std::string msgid();

};

struct freetds_conninfo {
  std::string ident = "";
  std::string dbname = "";
  std::string progname = "sqlsmith";
  std::string hostname = "";
  std::string username = "";
  std::string pass = "";

  freetds_conninfo(std::string ident,
                   std::string dbname,
                   std::string username,
                   std::string pass);

};

struct freetds_connection {
  DBPROCESS *conn = NULL;
  LOGINREC *login = NULL;
  freetds_connection(const freetds_conninfo &conninfo);
  void q(const char *query);
  ~freetds_connection();
};

struct freetds_schema : public freetds_connection, schema {
  freetds_schema(const freetds_conninfo &conninfo);

  void makeTables();
  void makeColumns();
  void makeTypes();
  void execStmt(std::string sql);

  virtual std::string quote_name(const std::string &id) {
    return id;
  }
};

struct dut_freetds : dut_base, freetds_connection {
  virtual void test(const std::string &stmt);
  dut_freetds(const freetds_conninfo &conninfo);
};

#endif //SQLSMITH_FREETDS_HH

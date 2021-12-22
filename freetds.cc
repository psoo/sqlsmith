#include <unistd.h> /* for gethostname() */
#include <sys/param.h> /* for MAXHOSTNAMELEN */

#include "config.h"
#include "dut.hh"
#include "freetds.hh"

static std::shared_ptr<freetds_msginfo_base> last_message = nullptr;
static std::shared_ptr<freetds_msginfo_base> last_error = nullptr;

/* Wrap handling of internal error/msg handler */
#define SQLSMITH_TDS_ERROR ((last_error != nullptr) ? last_error->msg() : "undefined error")
#define SQLSMITH_TDS_MESSAGE ((last_message != nullptr) ? last_message->msg() : "undefined message")

/*
 * TDS callback handlers
 *
 * Both, message and error handlers are initializing the internal
 * last_message handle. Use SQLSMITH_TDS_ERROR to access the message
 * contents of it.
 */

static int msg_handler(DBPROCESS *dbproc,
                       DBINT msgno,
                       int msgstate,
                       int severity,
                       char *msgtext,
                       char *srvname,
                       char *proname,
                       int line) {

  last_message = std::make_shared<freetds_msg>(dbproc,
                                               severity,
                                               msgno,
                                               msgstate,
                                               (msgtext) ? msgtext : "",
                                               (srvname) ? srvname : "",
                                               (proname) ? proname : "",
                                               line);
  return 0;

}

static int err_handler(DBPROCESS *dbproc,
                       int severity,
                       int dberr,
                       int oserr,
                       char *dberrstr,
                       char * oserrstr) {

  last_error = std::make_shared<freetds_err>(dbproc,
                                             severity,
                                             dberr,
                                             oserr,
                                             (dberrstr) ? dberrstr : "",
                                             (oserrstr) ? oserrstr : "");
  return INT_CANCEL;

}

freetds_err::freetds_err(DBPROCESS *dbproc,
                         int severity,
                         int dberr,
                         int oserr,
                         std::string dberrstr,
                         std::string oserrstr) : freetds_msginfo_base(dbproc, severity) {

  this->dberr = dberr;
  this->oserr = oserr;
  this->dberrstr = dberrstr;
  this->oserrstr = oserrstr;

}

std::string freetds_err::msg() {

  std::ostringstream oss;

  if (!dberrstr.empty()) {
    oss << dberrstr << ", db error " << dberr << std::endl;
  }

  if (!oserrstr.empty()) {
    oss << oserrstr << ", os error " << oserr << std::endl;
  }

  return oss.str();

}

std::string freetds_msg::msg() {

  std::ostringstream oss;

  if (msgno > 0) {
    oss << "msg " << (long) msgno << " severity " << severity << " state " << msgstate << std::endl;

    if (!ident.empty()) {
      oss << "server " << ident << std::endl;
    }

    if (!progname.empty()) {
      oss << "procedure name " << progname << std::endl;
    }

    if (line > 0) {
      oss << "line " << line << std::endl;
    }

    oss << msgtext << std::endl;

    if (severity > 10) {
      oss << "error: severity " << severity << std::endl;
    }

  }

  return oss.str();

}


freetds_msg::freetds_msg(DBPROCESS *dbproc,
                         int severity,
                         DBINT msgno,
                         int msgstate,
                         std::string msgtext,
                         std::string ident,
                         std::string progname,
                         int line) : freetds_msginfo_base(dbproc, severity) {

  this->msgno = msgno;
  this->msgstate = msgstate;
  this->msgtext = msgtext;
  this->ident = ident;
  this->progname = progname;
  this->line = line;

}

std::string freetds_msg::msgid() {
  std::ostringstream result;
  result << this->msgno;
  return result.str();
}

freetds_conninfo::freetds_conninfo(std::string ident,
                                   std::string dbname,
                                   std::string username,
                                   std::string pass) {

  char _hostname[MAXHOSTNAMELEN];
  this->ident = ident;
  this->dbname = dbname;
  this->username = username;
  this->pass = pass;
  this->progname = PACKAGE_NAME;

  this->hostname = std::string(_hostname, MAXHOSTNAMELEN);

}

freetds_connection::freetds_connection(const freetds_conninfo &conninfo) {

  if (dbinit() == FAIL) {
    throw dut::broken("could not initialize TDS connection");
  }

  dberrhandle(err_handler);
  dbmsghandle(msg_handler);

  /* Make login structure */
  if ((login = dblogin()) == FAIL) {
    throw dut::broken("could not allocate login structure for freetds");
  }

  if (conninfo.username.length() > 0)
    DBSETLUSER(login, conninfo.username.c_str());
  else
    throw dut::broken("username for freetds connection required");

  /* XXX: looks like there is no empty password allowed in freetds */
  if (conninfo.pass.length() > 0)
    DBSETLPWD(login, conninfo.pass.c_str());
  else
    throw dut::broken("password for freetds connection required");

  if (conninfo.hostname.length() > 0) {
    DBSETLHOST(login, conninfo.hostname.c_str());
  }

  if ((conn = dbopen(login, conninfo.ident.c_str())) == NULL) {
    throw std::runtime_error(SQLSMITH_TDS_ERROR);
  }

  if (dbuse(conn, conninfo.dbname.c_str()) == FAIL) {
    throw std::runtime_error(SQLSMITH_TDS_ERROR);
  }

}

freetds_connection::~freetds_connection() {
  if (conn != NULL)
    dbclose(conn);

  conn = NULL;
}

void freetds_connection::q(const char *query) {

  if (dbcmd(conn, query) == FAIL) {

    if (last_message->severity == 15) {
      throw dut::syntax(last_message->msg().c_str(),
                        std::dynamic_pointer_cast<freetds_msg>(last_message)->msgid().c_str());
    }

    throw dut::broken(last_message->msg().c_str(),
                      std::dynamic_pointer_cast<freetds_msg>(last_message)->msgid().c_str());
  }

  if (dbsqlexec(conn) == FAIL) {
    if ( (last_message->severity == 15) ) {
      throw dut::syntax(last_message->msg().c_str(),
                        std::dynamic_pointer_cast<freetds_msg>(last_message)->msgid().c_str());
    }

    throw dut::broken(last_message->msg().c_str(),
                      std::dynamic_pointer_cast<freetds_msg>(last_message)->msgid().c_str());
  }

  /*
   * We are required to consume _all_ rows from the result set
   * and there can be more than one result set (but not very likely
   * here. But anyways, tell the server we aren't interested in
   * the returned rows, so that we can proceed.
   * */
  while (dbresults(conn) != NO_MORE_RESULTS) {
    if (dbcancel(conn) == FAIL) {
      throw std::runtime_error(SQLSMITH_TDS_ERROR);
    }
  }
}

void freetds_schema::makeColumns() {

  std::cerr << "initialize table columns ... " << std::endl;

  for (auto table : tables) {

    /* query/result return code */
    int rc;

    std::cerr << " -> adding columns for table \"" << table.name << "\"" << std::endl;

    execStmt("SELECT schema_name(t.schema_id) AS type_schema, "
             "t.name AS type_name, "
             "c.name AS column_name "
             "FROM sys.columns AS c "
             "JOIN sys.types AS t ON c.user_type_id = t.user_type_id "
             "JOIN sys.tables tab ON tab.object_id = c.object_id "
             "WHERE tab.name = '" + table.name + "'");

    while ((rc = dbresults(conn)) != NO_MORE_RESULTS) {

      if (rc == FAIL) {
        throw std::runtime_error(SQLSMITH_TDS_ERROR);
      }

      while ((rc = dbnextrow(conn)) != NO_MORE_ROWS) {

        if (rc == FAIL) {
          throw std::runtime_error(SQLSMITH_TDS_ERROR);
        }
        column col((char *)dbdata(conn, 3), sqltype::get((char *)dbdata(conn, 2)));
        table.columns().push_back(col);

      }
    }

  }

  std::cerr << "done" << std::endl;

}

void freetds_schema::makeTables() {

  std::cerr << "initialize tables ... ";

  execStmt("SELECT schema_name(schema_id) AS schema_name, name AS table_name FROM sys.tables;");

  /*
   * Loop through the result set and fill in table definitions
   */
  while (dbresults(conn) != NO_MORE_RESULTS) {
    int rc;
    while ((rc = dbnextrow(conn)) != NO_MORE_ROWS) {
      std::string table_name((char *) dbdata(conn, 2));
      std::string schema_name((char *)dbdata(conn, 1));
      tables.push_back(table(table_name, schema_name, true, true));
    }
  }

  std::cerr << "done" << std::endl;

}

void freetds_schema::makeTypes() {

  std::cerr << "initialize data types ... ";

  execStmt("SELECT name, max_length, precision, scale FROM sys.types ORDER BY name;");

  while(dbresults(conn) != NO_MORE_RESULTS) {
    int rc;
    while ((rc = dbnextrow(conn)) != NO_MORE_ROWS) {
      types.push_back(sqltype::get( (char *) dbdata(conn, 1)));
    }
  }

  std::cerr << "done" << std::endl;

}

freetds_schema::freetds_schema(const freetds_conninfo &conninfo) : freetds_connection(conninfo) {

  /* Some basic initialization stuff */

  true_literal = "1";
  false_literal = "0";

  booltype     = sqltype::get("BIT");
  inttype      = sqltype::get("INTEGER");
  arraytype    = sqltype::get("ARRAY");
  internaltype = sqltype::get("internal");

  /* We define "internal" as something to be "anything" */
  internaltype = sqltype::get("sql_variant");

  /* List of types */
  makeTypes();

  /*
   * Get tables from database. This only includes user defined tables and excludes
   * system relation/view.
   */
  makeTables();

  /*
   * Make column lists with types.
   */
  makeColumns();

  generate_indexes();

}

void freetds_schema::execStmt(std::string sql) {

  std::cerr << "execute SQL " << sql << std::endl;
  if (sql.empty()) {
    throw std::runtime_error("attempt to execute empty SQL string");
  }

  if (dbcmd(conn, sql.c_str()) == FAIL) {
    throw std::runtime_error(SQLSMITH_TDS_ERROR);
  }

  if (dbsqlexec(conn) == FAIL) {
    throw std::runtime_error(SQLSMITH_TDS_ERROR);
  }

}

dut_freetds::dut_freetds(const freetds_conninfo &conninfo) : freetds_connection(conninfo) {
}

void dut_freetds::test(const std::string &stmt) {
  q(stmt.c_str());
}

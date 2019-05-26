#ifndef NDEBUG
#include <iostream>
#endif

#include "dal.h"
#include <cstring>
#include <cassert>
#include <sstream>
#include <ctime>



using namespace std;
using namespace aw_storm;

#ifdef DB_INIT
unordered_set<string> DbService::initializedTables;
#endif

// private ctor for move semantics
DbValue::DbValue() {}


DbValue::DbValue(const dbValuePrototype &prototype) {

	size_t length;
	const char *cstr;

	switch (prototype.type) {
		case DbDataType::DB_TEXT:
			if (prototype.size > 0) { // it's CString
				cstr = *static_cast<const char*const*>(prototype.field);
				if (!cstr)
					break;
				length = strlen(cstr) + 1;
			} else { // it's std::string
				const string* str = static_cast<const string*>(prototype.field);
				length = str->size() + 1;
				cstr = str->c_str();
			}

			if (length > sizeof(value_arr)) {
				value = new char[length];
				memcpy(value, cstr, length);
			} else {
				memcpy(value_arr, cstr, length);
			}
			break;
		case DbDataType::DB_TIME:
			strftime(value_arr, sizeof(value_arr), "%Y-%m-%d %H:%M:%S", static_cast<const tm*>(prototype.field)); // strftime in noexcept
			break;
		default: // everything else will fit into char[240]
			// we're letting various length signed integers to be stored but without an additional conversion this will work only on little endian machines
			if (prototype.type == DbDataType::DB_INTEGER && *(static_cast<char*>(prototype.field) + prototype.size - 1) & 128 ) // it's a negative number
				memset(value_arr, 255, sizeof (value_arr));

			memcpy(value_arr, prototype.field, prototype.size);
			break;
	}

	name = prototype.name;
	type = prototype.type;
	size = prototype.size;
}


DbValue::DbValue(const DbValue &other) {

	if (other.value == other.value_arr) {
		memcpy(value_arr, other.value_arr, sizeof(value_arr));
		value = value_arr;
	} else {
		size_t length = strlen(static_cast<char*>(other.value)) + 1;
		value = new char[length]; // might throw but there's nothing to clean up
		memcpy(value, other.value, length);
	}

	name = other.name;
	type = other.type;
	size = other.size;
}


DbValue::DbValue(DbValue &&other) : DbValue() {
	swap(*this, other);
}


DbValue::~DbValue() {
	if (value != value_arr)
		delete[] static_cast<char*>(value);
}


DbValue& DbValue::operator= (DbValue rhs) { // takes copy as an argument and therefore an exception may be arisen only before even entering the function
	swap(*this, rhs); // noexcept
	return *this;
}

namespace aw_storm {
void swap(DbValue &first, DbValue &second) noexcept {

	DbValue 	temp;

	if (first.value == first.value_arr) {
		memcpy(temp.value_arr, first.value_arr, sizeof(first.value_arr));
	} else
		temp.value = first.value;

	if (second.value == second.value_arr) {

		memcpy(first.value_arr, second.value_arr, sizeof(first.value_arr));
		first.value = first.value_arr;
	} else {
		first.value = second.value;
	}

	if (temp.value == temp.value_arr) { // default if not changed above
		memcpy(second.value_arr, temp.value_arr, sizeof(first.value_arr));
		second.value = second.value_arr;
	} else {
		second.value = temp.value;
	}

	temp.value = nullptr;

	using std::swap; // for ADL to work you have to use unqualified swap but in this scope it would mean aw_storm::swap() !
	swap(first.name, second.name);
	swap(first.type, second.type);
	swap(first.size, second.size);
}
} // namespace aw_storm


Retrievable::Retrievable(string tableName) : tableName(tableName), prototypes(*this) {}

Retrievable::Retrievable(const Retrievable &other) : Retrievable(other.tableName) {}

Retrievable::Retrievable(Retrievable &&other) : Retrievable(other.tableName) {}

Retrievable& Retrievable::operator=(const Retrievable &other) {
	this->tableName.assign(other.tableName);
	this->prototypes.~PrototypesAccessor();
	new (&this->prototypes) PrototypesAccessor(*this);
	return *this;
}

Retrievable& Retrievable::operator=(Retrievable &&other) {
	this->tableName.assign(other.tableName);
	this->prototypes.~PrototypesAccessor();
	new (&this->prototypes) PrototypesAccessor(*this);
	return *this;
}

DbValue Retrievable::GetValue(const string &name) {

	unordered_map<string, dbValuePrototype> &valuePrototypesMap = prototypes.GetPrototypesMap();

	unordered_map<string, dbValuePrototype>::const_iterator got = valuePrototypesMap.find(name);

	if (got == valuePrototypesMap.end())
		throw runtime_error("there's no property with that name");

	return DbValue(got->second);
}


vector<DbValue> Retrievable::GetValues() {

	vector<DbValue> results;

	vector<dbValuePrototype*> &valuePrototypesVector = prototypes.GetPrototypesVector();

	for (dbValuePrototype *prototype: valuePrototypesVector)
		results.push_back(DbValue(*prototype));
	return results;
}


string Retrievable::GetStrValue(const string &name) {

	unordered_map<string, dbValuePrototype> &valuePrototypesMap = prototypes.GetPrototypesMap();

	unordered_map<string, dbValuePrototype>::const_iterator got = valuePrototypesMap.find(name);

	if (got == valuePrototypesMap.end())
		throw runtime_error("there's no property with that name");

	string result;

	DbValue value(got->second);

	switch (value.type) {
		case DB_INTEGER:
			result = to_string(*(static_cast<int64_t*>(value.value)));
			break;
		case DB_FLOAT:
			assert(value.size == 8); // only double is supported
			result = to_string(*(static_cast<double*>(value.value)));
			break;

		case DB_TIME:
		case DB_TEXT:
			result = static_cast<const char*>(value.value);
			break;
	}
	return result;
}


const vector<dbValueSchema>& Retrievable::GetSchema() {

	return prototypes.GetSchema();
}


void Retrievable::SetValue(const string &name, void *value) {

	unordered_map<string, dbValuePrototype> &valuePrototypesMap = prototypes.GetPrototypesMap();

	unordered_map<string, dbValuePrototype>::iterator it;

	it = valuePrototypesMap.find(name);

	if (it == valuePrototypesMap.end())
		throw runtime_error("property with that name was not registered");

	switch (it->second.type) {
		case DB_TEXT: {
			size_t length(strlen(*static_cast<const char**>(value)) + 1);
			if (it->second.size > 0) { // it's a c-string
				char *cstr = new char[length];
				delete[] *static_cast<char**>(it->second.field); // !caution with this line!  additionaly automatic variables of char[] type are not supported since you can't delete[] them
				memcpy(cstr, *static_cast<const char**>(value), length);
				*static_cast<char**>(it->second.field) = cstr;
			} else {
				static_cast<string*>(it->second.field)->assign(*static_cast<const char**>(value)); //it must be null terminated
			}
			break;
		}
		case DB_TIME:
			strptime(*static_cast<const char**>(value), "%Y-%m-%d %H:%M:%S", static_cast<tm*>(it->second.field));
			break;
		default:
			memcpy(it->second.field, value, it->second.size);
			break;
	}
}


void Retrievable::SetStrValue(const string &name, const string &value) {

	unordered_map<string, dbValuePrototype> &valuePrototypesMap = prototypes.GetPrototypesMap();

	unordered_map<string, dbValuePrototype>::const_iterator got = valuePrototypesMap.find(name);

	if (got == valuePrototypesMap.end())
		throw runtime_error("there's no property with that name");

	switch (got->second.type) {
		case DB_INTEGER:
		{
			int64_t val = stol(value); // at the moment an exception from stol will propagate upward
			SetValue( got->second.name, &val );
			break;
		}
		case DB_FLOAT: {
			double val = stod(value);
			SetValue( got->second.name, &val );
			break;
		}
		case DB_TIME:
		case DB_TEXT: {
			const char* val = value.c_str();
			SetValue( got->second.name, &val );
			break;
		}
	}
}


namespace aw_storm {
string retrievable2JSON(Retrievable &entity) {

	vector<DbValue>			values(entity.GetValues());
	ostringstream			json;

	json << "{";
	for (vector<DbValue>::const_iterator itr = values.begin(), end = values.end(); itr != end; ++itr) {

		json << "\"" << itr->name << "\": ";
		switch (itr->type) {
			case DB_INTEGER:
				json << *(static_cast<int64_t*>(itr->value));
				break;

			case DB_FLOAT:
				json << *(static_cast<double*>(itr->value));
				break;

			case DB_TIME:
			case DB_TEXT:
				json << "\"" << static_cast<const char*>(itr->value) << "\"";
				break;
		}
		if (itr != end-1)
			json << ", ";
	}
	json << "}";

//	#ifndef NDEBUG
//	cout << json.str() << endl;
//	#endif

	return json.str();
}
} // namespace aw_storm


void Retrievable::SavePrototype(dbValuePrototype prototype) {

	unordered_map<string, dbValuePrototype> &valuePrototypesMap = prototypes.GetPrototypesMap(true);

	pair<unordered_map<string, dbValuePrototype>::iterator, bool>	ret;

	ret = valuePrototypesMap.insert(pair<string, dbValuePrototype>(prototype.name, prototype));
	assert(ret.second); //error: "value with that name is already registered"

	vector<dbValuePrototype*> &valuePrototypesVector = prototypes.GetPrototypesVector(true);

	valuePrototypesVector.push_back(&ret.first->second);

	vector<dbValueSchema> &schema = prototypes.GetSchema(true);
	schema.push_back({ret.first->second.name, ret.first->second.type, ret.first->second.size, ret.first->second.foreignKey});
}


Retrievable::PrototypesAccessor::PrototypesAccessor(Retrievable &parent) : parent(parent) {}

vector<dbValuePrototype*>& Retrievable::PrototypesAccessor::GetPrototypesVector(bool adding) { // declared: bool adding = false
	if (!adding && !schema.size())
		parent.Initialize();

	return valuePrototypesVector;
}


unordered_map<std::string, dbValuePrototype>& Retrievable::PrototypesAccessor::GetPrototypesMap(bool adding) { // declared: bool adding = false
	if (!adding && !schema.size())
		parent.Initialize();

	return valuePrototypesMap;
}


vector<dbValueSchema>& Retrievable::PrototypesAccessor::GetSchema(bool adding) { // declared: bool adding = false
	if (!adding && !schema.size())
		parent.Initialize();

	return schema;
}


DbService::DbService() : DbService("storm.db") {}


DbService::DbService(string connection) : connection(connection) {

	if (SQLITE_OK != sqlite3_initialize())
		throw runtime_error("Unable to initialize SQLite database");

	// creating databese file if it doesn't exist
	#ifdef DB_INIT
	sqlite3			*db = nullptr;
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}
	sqlite3_close(db);
	#endif
}


DbService::~DbService() {
	if (SQLITE_OK != sqlite3_shutdown())
		perror("Unable to correctly shutdown SQLite database");
}


void DbService::SaveEntity(Retrievable &entity) {
	#ifdef DB_INIT
	CheckDbSchema(entity);
	#endif


	vector<DbValue>		values(entity.GetValues());
	sqlite3			*db = nullptr;
	sqlite3_stmt		*stmt = nullptr;
	int			rc = -1;
	string			*sql(PrepareInsertSql(entity));

	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	rc = sqlite3_prepare_v2( db, sql->c_str(), -1, &stmt, NULL );
	if ( rc != SQLITE_OK) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot prepare stmt");
	}

	BindStmtParameters(stmt, values);

	// Foreign key constraints are disabled by default (for backwards compatibility), so must be enabled separately for each database connection
	#ifndef NDEBUG
	cout << "PRAGMA foreign_keys = ON" << endl;
	#endif
	sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	rc = sqlite3_step( stmt );
	if (( rc != SQLITE_DONE )&&( rc != SQLITE_ROW )) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot step stmt");
	}

	if (values[0].type == DB_INTEGER && !*(static_cast<int64_t*>(values[0].value))) { // The primary key calculated by the DB
		sqlite3_int64 rowid = sqlite3_last_insert_rowid( db );
		entity.SetValue(entity.GetSchema()[0].name, &rowid);
	}

	sqlite3_finalize( stmt ); // All of the prepared statements associated with a database connection must be finalized before the database connection can be closed
	sqlite3_close(db);

}


void DbService::SaveEntities(const vector<Retrievable*>& entities) { //TODO: maybe add check if all entities are from the same table...
	#ifdef DB_INIT
	CheckDbSchema(entities);
	#endif

	if (!entities.size())
		throw runtime_error("there was no entities to save");

	sqlite3			*db = nullptr;
	sqlite3_stmt		*stmt = nullptr;
	int			rc = -1;
	string			*sql;
	string			previous_name;

	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	// Foreign key constraints are disabled by default (for backwards compatibility), so must be enabled separately for each database connection
	#ifndef NDEBUG
	cout << "PRAGMA foreign_keys = ON" << endl;
	#endif
	sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	// begin transaction
	sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	// on live connection iterate over entities
	for (Retrievable *entity : entities) {

		if (entity->tableName != previous_name) {
			//prepare sql query
			sql = PrepareInsertSql(*entity);

			// prepare stmt on opened database
			rc = sqlite3_prepare_v2( db, sql->c_str(), -1, &stmt, NULL );
			if ( rc != SQLITE_OK) {
				sqlite3_finalize( stmt );
				sqlite3_close(db);
				throw runtime_error("Cannot prepare stmt");
			}
			previous_name = entity->tableName;
		}

		vector<DbValue> values = entity->GetValues();
		BindStmtParameters(stmt, values);

		// execute statement
		rc = sqlite3_step( stmt );
		if (( rc != SQLITE_DONE )&&( rc != SQLITE_ROW )) {
			sqlite3_finalize( stmt );
			sqlite3_close(db);
			throw runtime_error("Cannot step stmt");
		}

		if (values[0].type == DB_INTEGER && !*(static_cast<int64_t*>(values[0].value))) { // The primary key calculated by the DB
			sqlite3_int64 rowid = sqlite3_last_insert_rowid( db );
			entity->SetValue(values[0].name, &rowid);
		}

		sqlite3_reset( stmt ); // The function sqlite3_reset() simply resets a statement, it does not release it. To destroy a prepared statement and release its memory, the statement must be finalized.
		//we need to use clear_bindings because some of the entities might need PK to be bound and some don't
		sqlite3_clear_bindings( stmt ); // Contrary to the intuition of many, sqlite3_reset() does not reset the bindings on a prepared statement. Use this routine to reset all host parameters to NULL
	}

	// end transaction
	sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL);

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);
}


void DbService::UpdateEntity(Retrievable &entity) {
	#ifdef DB_INIT
	CheckDbSchema(entity);
	#endif

	vector<DbValue>		values(entity.GetValues());
	sqlite3			*db = nullptr;
	sqlite3_stmt		*stmt = nullptr;
	int			rc = -1;
	string			*sql(PrepareUpdateSql(entity));


	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	// prepare stmt on opened database
	rc = sqlite3_prepare_v2( db, sql->c_str(), -1, &stmt, NULL );
	if ( rc != SQLITE_OK) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot prepare stmt");
	}

	BindStmtParameters(stmt, values, true);

	// Foreign key constraints are disabled by default (for backwards compatibility), so must be enabled separately for each database connection
	#ifndef NDEBUG
	cout << "PRAGMA foreign_keys = ON" << endl;
	#endif
	sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	// execute statement
	if (sqlite3_step(stmt) != SQLITE_DONE) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot execute stmt");
	}

	//FillEntity(stmt, entity);

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);
}


void DbService::UpdateEntities(const std::vector<Retrievable*>& entities) {
	#ifdef DB_INIT
	CheckDbSchema(entities);
	#endif

	if (!entities.size())
		throw runtime_error("there was no entities to save");

	sqlite3			*db = nullptr;
	sqlite3_stmt		*stmt = nullptr;
	int			rc = -1;
	string			*sql;
	string			previous_name;

	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	// Foreign key constraints are disabled by default (for backwards compatibility), so must be enabled separately for each database connection
	#ifndef NDEBUG
	cout << "PRAGMA foreign_keys = ON" << endl;
	#endif
	sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	// begin transaction
	sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	// on live connection iterate over entities
	for (Retrievable *entity : entities) {

		if (entity->tableName != previous_name) {
			//prepare sql query
			sql = PrepareUpdateSql(*entity);

			// prepare stmt on opened database
			rc = sqlite3_prepare_v2( db, sql->c_str(), -1, &stmt, NULL );
			if ( rc != SQLITE_OK) {
				sqlite3_finalize( stmt );
				sqlite3_close(db);
				throw runtime_error("Cannot prepare stmt");
			}
			previous_name = entity->tableName;
		}

		vector<DbValue> values = entity->GetValues();
		BindStmtParameters(stmt, values, true);

		// execute statement
		rc = sqlite3_step( stmt );
		if (( rc != SQLITE_DONE )&&( rc != SQLITE_ROW )) {
			sqlite3_finalize( stmt );
			sqlite3_close(db);
			throw runtime_error("Cannot step stmt");
		}

		sqlite3_reset( stmt ); //The function sqlite3_reset() simply resets a statement, it does not release it. To destroy a prepared statement and release its memory, the statement must be finalized.
	}

	// end transaction
	sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL);

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);
}


void DbService::DeleteEntity(Retrievable &entity) {
	#ifdef DB_INIT
	CheckDbSchema(entity);
	#endif

	sqlite3			*db = nullptr;
	sqlite3_stmt		*stmt = nullptr;
	int			rc = -1;

	ostringstream		sql;
	sql << "DELETE FROM " << entity.tableName << " WHERE " << entity.GetSchema()[0].name << " = :" << entity.GetSchema()[0].name;

	#ifndef NDEBUG
	cout << sql.str() << endl;
	#endif

	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	// prepare stmt on opened database
	rc = sqlite3_prepare_v2( db, sql.str().c_str(), -1, &stmt, NULL );
	if ( rc != SQLITE_OK) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot prepare stmt");
	}

	// bind id to stmt parameter
	BindStmtParameter(stmt, entity.GetValues()[0]);

	// Foreign key constraints are disabled by default (for backwards compatibility), so must be enabled separately for each database connection
	#ifndef NDEBUG
	cout << "PRAGMA foreign_keys = ON" << endl;
	#endif
	sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	rc = sqlite3_step( stmt );
	if (( rc != SQLITE_DONE )&&( rc != SQLITE_ROW )) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot step stmt");
	}

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);
}


void DbService::DeleteEntities(const std::vector<Retrievable*>& entities) {
	#ifdef DB_INIT
	CheckDbSchema(entities);
	#endif

	if (!entities.size())
		throw runtime_error("there was no entities to save");

	sqlite3			*db = nullptr;
	sqlite3_stmt		*stmt = nullptr;
	int			rc = -1;
	string			previous_name;

	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	// Foreign key constraints are disabled by default (for backwards compatibility), so must be enabled separately for each database connection
	#ifndef NDEBUG
	cout << "PRAGMA foreign_keys = ON" << endl;
	#endif
	sqlite3_exec(db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);

	// begin transaction
	sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

	// on live connection iterate over entities
	for (Retrievable *entity : entities) {

		if (entity->tableName != previous_name) {
			//prepare sql query
			ostringstream	sql;
			sql << "DELETE FROM " << entity->tableName << " WHERE " << entity->GetSchema()[0].name << " = :" << entity->GetSchema()[0].name;

			#ifndef NDEBUG
			cout << sql.str() << endl;
			#endif

			// prepare stmt on opened database
			rc = sqlite3_prepare_v2( db, sql.str().c_str(), -1, &stmt, NULL );
			if ( rc != SQLITE_OK) {
				sqlite3_finalize( stmt );
				sqlite3_close(db);
				throw runtime_error("Cannot prepare stmt");
			}

			previous_name = entity->tableName;
		}

		// bind id to stmt parameter
		BindStmtParameter(stmt, entity->GetValues()[0]);

		// execute statement
		rc = sqlite3_step( stmt );
		if (( rc != SQLITE_DONE )&&( rc != SQLITE_ROW )) {
			sqlite3_finalize( stmt );
			sqlite3_close(db);
			throw runtime_error("Cannot step stmt");
		}

		sqlite3_reset( stmt ); //The function sqlite3_reset() simply resets a statement, it does not release it. To destroy a prepared statement and release its memory, the statement must be finalized.
	}

	// end transaction
	sqlite3_exec(db, "END TRANSACTION", NULL, NULL, NULL);

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);
}


void DbService::RetrieveEntity(Retrievable& entity) {
	#ifdef DB_INIT
	CheckDbSchema(entity);
	#endif

	sqlite3							*db = nullptr;
	sqlite3_stmt						*stmt = nullptr;
	int							rc = -1;
	string							sql(*PrepareSelectSql(entity));

	sql.append(" WHERE " + entity.GetSchema()[0].name + " = :" + entity.GetSchema()[0].name);

	#ifndef NDEBUG
	cout << sql << endl;
	#endif

	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	// prepare stmt on opened database
	rc = sqlite3_prepare_v2( db, sql.c_str(), -1, &stmt, NULL );
	if ( rc != SQLITE_OK) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot prepare stmt");
	}

	// bind id to stmt parameter
	BindStmtParameter(stmt, entity.GetValues()[0]);

	// execute statement
	rc = sqlite3_step(stmt);

	if (( rc != SQLITE_DONE )&&( rc != SQLITE_ROW )) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw runtime_error("Cannot execute stmt");
	}

	assert(rc == SQLITE_ROW);

	FillEntity(stmt, entity);

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);
}


string* DbService::SearchSqlCache(const string &tableName, unordered_map<string, string> &cache) {

	unordered_map<string, string>::iterator	it;
	string			*result = nullptr;

	it = cache.find(tableName);

        if (it != cache.end())
                result = &it->second;

        return result;
}


string* DbService::PrepareInsertSql(Retrievable &entity) {

	string		*result(SearchSqlCache(entity.tableName, sqlInsertCache));

	if (!result) {

		const vector<dbValueSchema>				&columns(entity.GetSchema());

		assert(columns.size() > 1); //TODO: przeniesc gdzeis wyzej to sprawdzanie

		ostringstream						sql_first_part;
		ostringstream						sql_second_part;
		pair<unordered_map<string, string>::iterator, bool> 	ret;

		sql_first_part << "INSERT INTO " << entity.tableName << " ( ";
		for (vector<dbValueSchema>::const_iterator itr = columns.begin(), end = columns.end(); itr != end; ++itr) { // we don't need to skip first value (PK) because unbound parameters are interpreted as NULL
			sql_first_part << itr->name;
			sql_second_part << ":" << itr->name;

			if (itr != end-1) {
				sql_first_part << ", ";
				sql_second_part << ", ";
			}
		}
		sql_first_part << " ) VALUES ( ";
		sql_second_part << " )";

		sql_first_part << sql_second_part.str();

		#ifndef NDEBUG
		cout << sql_first_part.str() << endl;
		#endif

		ret = sqlInsertCache.insert(pair<string, string>(entity.tableName, sql_first_part.str().c_str()));

		assert(ret.second);

		result = &ret.first->second;
	}

	return result;
}


string* DbService::PrepareSelectSql(Retrievable &entity) {

	string		*result(SearchSqlCache(entity.tableName, sqlSelectCache));

	if (!result) {

		const vector<dbValueSchema>				&columns(entity.GetSchema());

		assert(columns.size() > 1); //TODO: przeniesc gdzeis wyzej to sprawdzanie

		ostringstream						sql;
		pair<unordered_map<string, string>::iterator, bool> 	ret;

		sql << "SELECT ";
		for (vector<dbValueSchema>::const_iterator itr = columns.begin(), end = columns.end(); itr != end; ++itr) {
			sql << itr->name;

			if (itr != end-1)
				sql << ", ";
		}
		sql << " FROM " << entity.tableName;

		#ifndef NDEBUG
		cout << sql.str() << endl;
		#endif

		ret = sqlSelectCache.insert(pair<string, string>(entity.tableName, sql.str().c_str()));

		assert(ret.second);

		result = &ret.first->second;
	}

	return result;
}


string* DbService::PrepareUpdateSql(Retrievable &entity) {

	string		*result(SearchSqlCache(entity.tableName, sqlUpdateCache));

	if (!result) {

		const vector<dbValueSchema>				&columns(entity.GetSchema());

		assert(columns.size() > 1); //TODO: przeniesc gdzeis wyzej to sprawdzanie


		ostringstream						sql;
		pair<unordered_map<string, string>::iterator, bool> 	ret;


		sql << "UPDATE " << entity.tableName << " SET ";
		for (vector<dbValueSchema>::const_iterator itr = columns.begin() + 1, end = columns.end(); itr != end; ++itr) { // we're bypassing first value as this is PK
			sql << itr->name << " = :" << itr->name;

			if (itr != end-1)
				sql << ", ";
		}
		sql << " WHERE " << columns[0].name << " = :" <<  columns[0].name;

		#ifndef NDEBUG
		cout << sql.str() << endl;
		#endif

		ret = sqlUpdateCache.insert(pair<string, string>(entity.tableName, sql.str().c_str()));

		assert(ret.second);

		result = &ret.first->second;
	}

	return result;
}

void DbService::BindStmtParameter(sqlite3_stmt *stmt, const DbValue &value) {

	int				rc = -1;
	string				named_parameter(":" + value.name);
	int				idx(sqlite3_bind_parameter_index(stmt, named_parameter.c_str()));

	switch (value.type) {
		case DB_INTEGER:
			rc = sqlite3_bind_int64(stmt, idx, *(static_cast<int64_t*>(value.value))); // INTEGER. The value is a signed integer, stored in 1, 2, 3, 4, 6, or 8 bytes depending on the magnitude of the value.
			assert(rc == SQLITE_OK);
			break;
		case DB_FLOAT:
			assert(value.size == 8); // only double is supported
			rc = sqlite3_bind_double(stmt, idx, *(static_cast<double*>(value.value)));
			assert(rc == SQLITE_OK);
			break;

		case DB_TIME:
		case DB_TEXT:
			rc = sqlite3_bind_text(stmt, idx, static_cast<const char*>(value.value), -1, SQLITE_STATIC );  //if SQLITE_STATIC is passed sqlite won't release memory for us
			assert(rc == SQLITE_OK);
			break;
	}
}


void DbService::BindStmtParameters(sqlite3_stmt *stmt, const vector<DbValue> &values, bool withPK) { // declared: withPK = false

	assert(values.size() > 1);

	withPK = withPK || !(values[0].type == DB_INTEGER && !*(static_cast<int64_t*>(values[0].value)));

	for (vector<DbValue>::const_iterator itr = values.begin() + !withPK, end = values.end(); itr != end; ++itr) { // if withPK is set to false we're bypassing the first value as this is PK

		BindStmtParameter(stmt, *itr);
	}
}


void DbService::FillEntity(sqlite3_stmt* stmt, Retrievable &entity) {

	const vector<dbValueSchema>		&columns(entity.GetSchema());

	//loop over columns
	size_t column_count;
	for (int i = 0, column_count = columns.size(); i < column_count; ++i) {
		switch (columns[i].type) {
			case DB_INTEGER: {
				int64_t ret = sqlite3_column_int64(stmt, i); // (...) But as soon as INTEGER values are read off of disk and into memory for processing, they are converted to the most general datatype (8-byte signed integer)
				entity.SetValue( columns[i].name, &ret );
				break;
			}
			case DB_FLOAT: {
				assert(columns[i].size == 8); // only double is supported
				double ret = sqlite3_column_double(stmt, i);
				entity.SetValue( columns[i].name, &ret );
				break;
			}
			case DB_TIME:
			case DB_TEXT: {
				const unsigned char* ret = sqlite3_column_text(stmt, i);
				entity.SetValue( columns[i].name, &ret );
				break;
			}
		} //end switch
	} //end loop
}


#ifdef DB_INIT
void DbService::CheckDbSchema(Retrievable& entity) {

	unordered_set<string>::const_iterator got = DbService::initializedTables.find(entity.tableName);

	if (got == DbService::initializedTables.end()) {

		ostringstream			sql;
		sql << "PRAGMA table_info('" << entity.tableName << "')";

		#ifndef NDEBUG
		cout << sql.str() << endl;
		#endif

		sqlite3				*db = nullptr;
		sqlite3_stmt			*stmt = nullptr;
		int				rc = -1;
		const vector<dbValueSchema>	&columns(entity.GetSchema());
		size_t				column_count;
		bool				inconsistent = false;

		assert(columns.size() > 1);


		// open database
		if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
			sqlite3_close(db);
			throw runtime_error("Cannot open SQLite connection");
		}

		// prepare stmt on opened database
		rc = sqlite3_prepare_v2( db, sql.str().c_str(), -1, &stmt, NULL );
		if ( rc != SQLITE_OK) {
			sqlite3_finalize( stmt );
			sqlite3_close(db);
			throw runtime_error("Cannot prepare stmt");
		}


		for (int i = 0, column_count = columns.size(); i < column_count; ++i) {
			if (sqlite3_step( stmt ) != SQLITE_ROW) {
				inconsistent = true;
				break;
			}

			if (columns[i].name.compare(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)))) {
				inconsistent = true;
				break;
			}

			string type(dbDataType2string(columns[i].type));

			if (type.compare(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)))) {
				inconsistent = true;
				break;
			}

			if (i == 0) {
				if (sqlite3_column_int(stmt, 5) == 0 && columns[i].foreignKey.size() == 0) {
					inconsistent = true;
					break;
				}
			}
		}

		if (sqlite3_step( stmt ) != SQLITE_DONE)
			inconsistent = true;

		// finalize stmt before closing database
		sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

		// close database
		sqlite3_close(db);

		if (inconsistent)
			RecreateTable(entity);

		DbService::initializedTables.insert(entity.tableName);
	}
}


void DbService::CheckDbSchema(const vector<Retrievable*>& entities) {
	string		previous_name;

	for (Retrievable* entity: entities) {
		if (entity->tableName != previous_name) {
			CheckDbSchema(*entity);
		previous_name = entity->tableName;
		}
	}
}


void DbService::RecreateTable(Retrievable& entity) {

	sqlite3				*db = nullptr;
	const vector<dbValueSchema>	&columns(entity.GetSchema());
	string				sql;

	assert(columns.size() > 1);

	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw runtime_error("Cannot open SQLite connection");
	}

	sql = "DROP TABLE IF EXISTS " + entity.tableName + "; CREATE TABLE " + entity.tableName + " ( ";

	size_t column_count;
	for (int i = 0, column_count = columns.size(); i < column_count; ++i) {
		sql += columns[i].name + " " + dbDataType2string(columns[i].type);
		if (i == 0) {
			if (columns[i].foreignKey.length())
				sql += " UNIQUE NOT NULL REFERENCES " + columns[i].foreignKey;
			else
				sql += " PRIMARY KEY NOT NULL";

		} else if (columns[i].foreignKey.length())
			sql += " REFERENCES " + columns[i].foreignKey;

		if (i + 1 < column_count)
			sql += ", ";
	}
	sql += " );";

	#ifndef NDEBUG
	cout << sql << endl;
	#endif

	sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL);

	sqlite3_close(db);
}


string DbService::dbDataType2string(DbDataType type) {
	switch (type) {
		case DB_INTEGER:
			return {"integer"};
		case DB_FLOAT:
			return {"float"};
		case DB_TIME:
		case DB_TEXT:
			return {"text"};
	}
}
#endif

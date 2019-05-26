#ifndef AW_LIBDAL_H
#define AW_LIBDAL_H

#include "sqlite3.h"

#include <cstddef> // for size_t
#include <string>
#include <vector>
#include <utility>
#include <map>
#include <type_traits> // for enable_if<T>, is_integral<T>, remove_pointer<T> ...
#include <limits> // for numeric_limits
#include <unordered_map> // for unordered_map
#include <ctime> // for tm

#ifdef DB_INIT
#include <unordered_set>
#include <array>
#endif

#ifndef NDEBUG
#include <iostream>
#endif

namespace aw_storm {

enum DbDataType { DB_INTEGER, DB_FLOAT, DB_TEXT, DB_TIME };

struct dbValuePrototype {
	const std::string name;
	const DbDataType type;
	void *const field;
	const size_t size;
	const std::string foreignKey;
};


class DbValue {
public:
	DbValue(const dbValuePrototype&);
	DbValue(const DbValue&);
	DbValue(DbValue&&);
	~DbValue();

	DbValue& operator= (DbValue); // by value
	friend void swap(DbValue&, DbValue&) noexcept;

	std::string name;
	DbDataType type;
	void * value = value_arr;
	size_t size;

private:
	DbValue(); // for move semantics
	char value_arr[240] = {0};
};


struct dbValueSchema {
	const std::string &name;
	const DbDataType type;
	const size_t size;
	const std::string &foreignKey;
};


class Retrievable {
public:
	Retrievable(std::string tableName);

	// needed so the implicit copy and move c'tors won't be invoked
	Retrievable(const Retrievable &other);
	Retrievable(Retrievable &&other);
	Retrievable& operator=(const Retrievable &other);
	Retrievable& operator=(Retrievable &&other);

	DbValue GetValue(const std::string&);
	std::vector<DbValue> GetValues();
	std::string GetStrValue(const std::string&);
	const std::vector<dbValueSchema>& GetSchema();

	void SetValue(const std::string&, void*);
	void SetStrValue(const std::string &name, const std::string &value);
	std::string tableName;

	// function accessible through ADL thanks to the argument of type of a class in which this function is declared: "The idea behind ADL is to look for a declaration of the function being called in the associated classes and associated namespaces for each type in the argument list"
	friend std::string retrievable2JSON(Retrievable&);

protected:
	template <typename T, typename std::enable_if<std::is_integral<typename std::remove_pointer<T>::type>::value && std::numeric_limits<typename std::remove_pointer<T>::type>::is_signed || sizeof(typename std::remove_pointer<T>::type) < 8>::type* = nullptr>
	void RegisterField(std::string, T);

	template <typename TEntity, typename T, typename std::enable_if<std::is_integral<typename std::remove_pointer<T>::type>::value && std::numeric_limits<typename std::remove_pointer<T>::type>::is_signed || sizeof(typename std::remove_pointer<T>::type) < 8>::type* = nullptr>
	void RegisterField(std::string, T);

	template <typename T, typename std::enable_if<std::is_floating_point<typename std::remove_pointer<T>::type>::value>::type* = nullptr>
	void RegisterField(std::string, T);

	template <typename T, typename std::enable_if<std::is_pointer<typename std::remove_pointer<T>::type>::value>::type* = nullptr>
	void RegisterField(std::string, T);

	template <typename T, typename std::enable_if<std::is_same<typename std::remove_pointer<T>::type, typename std::string>::value>::type* = nullptr>
	void RegisterField(std::string, T);

	template <typename T, typename std::enable_if<std::is_same<typename std::remove_pointer<T>::type, typename std::tm>::value>::type* = nullptr>
	void RegisterField(std::string, T);

private:
	virtual void Initialize() = 0;

	void SavePrototype(dbValuePrototype);

	class PrototypesAccessor {
	public:
		PrototypesAccessor(Retrievable&);
		std::vector<dbValuePrototype*>& GetPrototypesVector(bool adding = false);
		std::unordered_map<std::string, dbValuePrototype>& GetPrototypesMap(bool adding = false);
		std::vector<dbValueSchema>& GetSchema(bool adding = false);
	private:
		Retrievable &parent;
		std::unordered_map<std::string, dbValuePrototype> valuePrototypesMap;
		std::vector<dbValuePrototype*> valuePrototypesVector;
		std::vector<dbValueSchema> schema;
	};

	PrototypesAccessor prototypes;
};


class DbService {
public:
	DbService();
	DbService(std::string connection);
	~DbService();
	void SaveEntity(Retrievable&);
	void SaveEntities(const std::vector<Retrievable*>&);

	void UpdateEntity(Retrievable&);
	void UpdateEntities(const std::vector<Retrievable*>&);

	void DeleteEntity(Retrievable&);
	void DeleteEntities(const std::vector<Retrievable*>&);

	void RetrieveEntity(Retrievable&);
	template <class TEntity>
	std::vector<TEntity> RetrieveEntities(const std::string &predicate);
	template <class TEntity>
	std::vector<TEntity> RetrieveChildEntities(Retrievable&);

	#ifdef DB_INIT
	static std::unordered_set<std::string> initializedTables;
	#endif

private:
	std::string* SearchSqlCache(const std::string&, std::unordered_map<std::string, std::string> &cache);

	template <class TEntity>
	std::string* PrepareInsertSql();
	std::string* PrepareInsertSql(Retrievable&);

	template <class TEntity>
	std::string* PrepareSelectSql();
	std::string* PrepareSelectSql(Retrievable&);

	template <class TEntity>
	std::string* PrepareUpdateSql();
	std::string* PrepareUpdateSql(Retrievable&);

	void BindStmtParameter(sqlite3_stmt*, const DbValue&);
	void BindStmtParameters(sqlite3_stmt*, const std::vector<DbValue>&, bool withPK = false);

	void FillEntity(sqlite3_stmt*, Retrievable&);

	template <class TEntity>
	TEntity FillEntity(sqlite3_stmt*);


	const std::string connection;

	std::unordered_map<std::string, std::string> sqlInsertCache;
	std::unordered_map<std::string, std::string> sqlSelectCache;
	std::unordered_map<std::string, std::string> sqlUpdateCache;

	#ifdef DB_INIT
	void CheckDbSchema(Retrievable&);
	void CheckDbSchema(const std::vector<Retrievable*>&);
	template <class TEntity>
	void CheckDbSchema();
	void RecreateTable(Retrievable&);
	std::string dbDataType2string(DbDataType);
	#endif
};


// templated member functions definitions

template <typename T, typename std::enable_if<std::is_integral<typename std::remove_pointer<T>::type>::value && std::numeric_limits<typename std::remove_pointer<T>::type>::is_signed || sizeof(typename std::remove_pointer<T>::type) < 8>::type*>
void Retrievable::RegisterField(std::string name, T address) {

        dbValuePrototype prototype = {
                name,
                DbDataType::DB_INTEGER,
                static_cast<void*>(address),
                sizeof(typename std::remove_pointer<T>::type)
        };

        SavePrototype(prototype);
}


template <typename TEntity, typename T, typename std::enable_if<std::is_integral<typename std::remove_pointer<T>::type>::value && std::numeric_limits<typename std::remove_pointer<T>::type>::is_signed || sizeof(typename std::remove_pointer<T>::type) < 8>::type*>
void Retrievable::RegisterField(std::string name, T address) {

	TEntity entity;
        dbValuePrototype prototype = {
                name,
                DbDataType::DB_INTEGER,
                static_cast<void*>(address),
                sizeof(typename std::remove_pointer<T>::type),
		entity.tableName
        };

        SavePrototype(prototype);
}


template <typename T, typename std::enable_if<std::is_floating_point<typename std::remove_pointer<T>::type>::value>::type*>
void Retrievable::RegisterField(std::string name, T address) {

        dbValuePrototype prototype = {
                name,
                DbDataType::DB_FLOAT,
                static_cast<void*>(address),
                sizeof(typename std::remove_pointer<T>::type)
        };

        SavePrototype(prototype);
}


template <typename T, typename std::enable_if<std::is_pointer<typename std::remove_pointer<T>::type>::value>::type*>
void Retrievable::RegisterField(std::string name, T address) {

        dbValuePrototype prototype = {
                name,
                DbDataType::DB_TEXT,
                static_cast<void*>(address),
                sizeof(typename std::remove_pointer<typename std::remove_pointer<T>::type>::type)
        };

        SavePrototype(prototype);
}


template <typename T, typename std::enable_if<std::is_same<typename std::remove_pointer<T>::type, typename std::string>::value>::type*>
void Retrievable::RegisterField(std::string name, T address) {

        dbValuePrototype prototype = {
                name,
                DbDataType::DB_TEXT,
                static_cast<void*>(address),
                0
        };

        SavePrototype(prototype);
}

template <typename T, typename std::enable_if<std::is_same<typename std::remove_pointer<T>::type, typename std::tm>::value>::type*> //@@@@@@dopisac logike do DB_TIME!!!
void Retrievable::RegisterField(std::string name, T address) {

        dbValuePrototype prototype = {
                name,
                DbDataType::DB_TIME,
                static_cast<void*>(address),
                0
        };

        SavePrototype(prototype);
}

template <class TEntity>
std::vector<TEntity> DbService::RetrieveEntities(const std::string &predicate) {
	#ifdef DB_INIT
	CheckDbSchema<TEntity>();
	#endif

	sqlite3				*db = nullptr;
	sqlite3_stmt			*stmt = nullptr;
	int				rc = -1;
	std::string			sql(*PrepareSelectSql<TEntity>());
	std::vector<TEntity>		result;

	sql.append(" WHERE " + predicate);

	#ifndef NDEBUG
	std::cout << sql << std::endl;
	#endif

	// open database
	if (SQLITE_OK != sqlite3_open_v2(connection.c_str(), &db, SQLITE_OPEN_READWRITE, NULL)) {
		sqlite3_close(db);
		throw std::runtime_error("Cannot open SQLite connection");
	}

	// prepare stmt on opened database
	rc = sqlite3_prepare_v2( db, sql.c_str(), -1, &stmt, NULL );
	if ( rc != SQLITE_OK) {
		sqlite3_finalize( stmt );
		sqlite3_close(db);
		throw std::runtime_error("Cannot prepare stmt");
	}

	//execute stmt and loop over resulting rows
	while ( (rc = sqlite3_step(stmt)) == SQLITE_ROW ) {
		result.push_back(FillEntity<TEntity>(stmt));
	} //end row loop

	// finalize stmt before closing database
	sqlite3_finalize( stmt ); //All of the prepared statements associated with a database connection must be finalized before the database connection can be closed

	// close database
	sqlite3_close(db);

	if (rc != SQLITE_DONE)
		throw std::runtime_error("error executing statement");

	return result;
}

template <class TEntity>
std::vector<TEntity> DbService::RetrieveChildEntities(Retrievable& parent) {

	TEntity					entity;
	const std::vector<dbValueSchema>	&columns(entity.GetSchema());
	std::string				predicate;
	std::vector<TEntity>			result;

	for (const dbValueSchema &column: columns) {
		if (column.foreignKey == parent.tableName) {
			predicate = column.name + " = ";
			predicate.append(std::to_string(*(static_cast<int64_t*>(parent.GetValues()[0].value))));
			break;
		}
	}

	if (predicate.size())
		result = RetrieveEntities<TEntity>(predicate);

	return result;
}


template <class TEntity>
std::string* DbService::PrepareInsertSql() {
	TEntity entity;
	return PrepareInsertSql(entity);
}


template <class TEntity>
std::string* DbService::PrepareSelectSql() {
	TEntity entity;
	return PrepareSelectSql(entity);
}


template <class TEntity>
std::string* DbService::PrepareUpdateSql() {
	TEntity entity;
	return PrepareUpdateSql(entity);
}


template <class TEntity>
TEntity DbService::FillEntity(sqlite3_stmt* stmt) {
	TEntity entity;
	FillEntity(stmt, entity);
	return entity;
}


#ifdef DB_INIT
template <class TEntity>
void DbService::CheckDbSchema() {
	TEntity entity;
	CheckDbSchema(entity);
}
#endif
// end of templated functions

} // namespace aw_storm
#endif

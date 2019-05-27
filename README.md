# STORM
STORM --- Super Tiny ORM for SQLite.
This library is a C++ wrapper for the SQLite API providing easy to use and simplified ORM semantics.
As an Object-Relational Mapping framework it provides a layer of abstraction over underlying database CRUD operations freeing a programmer from the hassle of constructing SQL statements and managing database connections.

```c++
Person person;
person.name = "June";
person.surname = "Moone";
strptime("17 Aug 1988", "%d %b %Y", &person.dob);
person.occupation = "archeologist";
person.personalities = 2;

db.SaveEntity(person);
cout << "Person's Id assigned by the database: " << person.person_id << endl;
```

## Augmenting a POD class
A class representing a database entity has to meet two requirements to be used with STORM framework:
 1. It needs to publicly inherit from the `Retrievable` type and,
 2. It needs to register its fields (columns) in the private virtual `Initialize` function.

That's it.
Let's make a sample class to comply with the above. First we start with a plain old data class which reflects a database table record:

```c++
class Person {
public:
	int64_t person_id = 0; // <-- Primary Key
	string name;
	string surname;
	tm dob;
	string occupation;
	int personalities;
};
```

> NOTE: There is nothing to prevent us from making the fields private and introducing logic to the class (being a POD is not a requirement just a simplest example of representing a record in a table).

Next derive this class from the Retrievable type and call the Retrievable's constructor passing it the name of the table this class is to represent:

```c++
class Person : public Retrievable {
public:
	Person() : Retrievable("Persons") {}
};
```

Now we have to register its fields (columns). We do this with the `RegisterField(...)` function. Its prototype looks roughly like this:

```c++
void RegisterField( column_name, field_address );
```

And we place calls to it inside the private virtual `Initialize()` function:
```c++
class Person : public Retrievable {
public:
	Person() : Retrievable("Persons") {}
	int64_t person_id = 0; // <-- Primary Key
	string name;
	string surname;
	tm dob;
	string occupation;
	int personalities;
private:
	void Initialize() override {
		RegisterField("person_id", &person_id); // to indicate that a field is a PK we register it first
		RegisterField("name", &name);
		RegisterField("surname", &surname);
		RegisterField("birthday", &dob); // table columns don't need to have the same names as fields
		RegisterField("occupation", &occupation);
		RegisterField("personalities", &personalities);
	}
};
```

And now we have a fully functional STORM entity class. We can do all the CRUD operations with it:

```c++
Person person = getJuneBeforeSheTurns();

DbService db; // our database (defaults to "storm.db", see later on)

// C
db.SaveEntity(person);
assert(person.person_id); // the database assigned an Id to the newly saved entity

// R
Person emptyPerson;
emptyPerson.person_id = person.person_id;
db.RetrieveEntity(emptyPerson);
assert(emptyPerson.name == "June");

// U
person.name.assign("Enchantress");
db.UpdateEntity(person);

// D
db.DeleteEntity(person);
```

## Relations
> NOTE: STORM always sets the SQLite `PRAGMA foreign_keys = ON` so every foreign key constraint introduced to a database schema is enforced.

To represent a foreign key constraint in an entity class we register appropriate field with the templated version of the `RegisterField()` function setting as the template argument the type which the foreign key refers to:

```c++
class BusinessTrip : public Retrievable {
public:
	BusinessTrip() : Retrievable("Trips") {}
	int64_t businessTrip_id;
	string destination;
	double expenses;
	int64_t person_id;
private:
	void Initialize() override {
		RegisterField("trip_id", &businessTrip_id); // <-- Primary Key
		RegisterField("destination", &destination);
		RegisterField("expenses", &expenses);
		RegisterField<Person>("person_id", &person_id); // <-- Foreign Key
	}
};
```

Now we can retrieve a collection of children of a particular parent entity:

```c++
Person person1 = getJuneBeforeSheTurns();
db.SaveEntity(person1);

BusinessTrip trip1, trip2;
trip1.destination = "Tres Osos Caves";
trip1.expenses = 100000;
trip1.person_id = person1.person_id; // foreign key to person1

trip2.destination = "Iran";
trip2.expenses = 0;
trip2.person_id = person1.person_id; // foreign key to person1

vector<Retrievable*> bulk{&trip1, &trip2};
db.SaveEntities(bulk);

vector<BusinessTrip> childEntities = db.RetrieveChildEntities<BusinessTrip>(person1);
```

## DbService

A "_connection_" to an SQLite database is represented by the `DbService` object. Its constructor takes the path to the database file. When constructed with the default constructor it looks for the `storm.db` file.

```c++
DbService db("database_dir/database_file"); // when a path starts with the '/' it's absolute otherwise it's relative

// now we can perform operations on the database

vector<Person> results = db.RetrieveEntities<Person>("name LIKE 'Moon%'");
```

## Transactions and bulk operations

We can write multiple entities in one atomic transaction. To do this we're passing the `std::vector` consisting of pointers to the entities to the `SaveEntities` function. It's worth noting that entities don't have to be of the same type.

```c++
Person person1;
person1.person_id = 333; // this time we're setting the Id by hand so it can't be already taken

BusinessTrip trip1, trip2;
trip1.person_id = person1.person_id;
trip2.person_id = person1.person_id;

vector<Retrievable*> ourEntities = { &person1, &trip1, &trip2 };
db.SaveEntities(ourEntities);
```

## Code first

If we will compile the STORM library with the `DB_INIT` flag (`make dbinit`) then the library will take responsibility for creating the database file if it doesn't exist and for updating database schema where necessary.
> **NOTE 1:** it won't create directories if the path to the database file includes some. They need to be present already.
> **NOTE 2:** tables with the same name and a different schema will be dropped!


## Concurrency control

SQLite doesn't offer a native support for the _optimistic concurrency control_ (with timestamps, sequence numbers and such) thus STORM doesn't offer it either. Regarding the SQLite locking mechanism it is yet to be implemented in STORM.

## Thread safety

STORM is **not** thread-safe. All synchronization responsibilities rest with a user of the STORM library. This is by design as STORM is intended as a lightweight wrapper.

## Example

For more use cases see the [main.cpp](examples/main.cpp) file in the [examples](examples/) directory. To run it just navigate to the [examples](examples/) directory, type `make dbinit` and execute the created `runme` file:

```c++
$ git clone https://github.com/listerreg/storm.git
$ cd storm/examples
$ make dbinit
$ ./runme
```

## Licensing
This project is licensed under the terms of the MIT license.

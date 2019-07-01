#include "dal.h" // Header of our library
#include <iostream>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cassert>

using namespace aw_storm;

/*
A sample class representing table in a database could be like below:

class Host {
public:
	// with accessors...
	int GetAsset();
	void SetAsset(int);
	double GetPrice();
	void SetPrice(double)

	// ...or with fields
	int64_t host_id = 0;
	string hostName;
	char *addressIP = nullptr;
private:
	int asset = 0;
	double price = 0;
};

This is how we can achieve this using STORM.

First, our class needs to publicly derive from the Retrievable type:

class Host : public Retrievable (...)


Second, we need to call the base class constructor passing the name of the associated table in the database.

Host() : Retrievable("Hosts") {}

Third, we need to register fields that represent columns. We do this inside the Initialize() virtual function with a call to the RegisterField(...) function for each field. Parameters for the RegisterField() are the name of the column and the address (pointer) of the field.
The class have to have a field that will serve as the Primary Key in the database. To declare a field as the PK you register it as the first one.

virtual void Initialize() {
	RegisterField("host_id", &host_id); // <-- Primary Key
	RegisterField("host_name", &hostName);
	// ...
}


STORM (as it depends on SQLite) let you use following fields for storing in a database:

- signed integers not longer than 64 bits
- unsigned integers not longer than 32 bits (SQLite doesn't support unsigned numbers so we can only sore them as 8-byte signed)
- double
- char*
- string
- tm (date and time)

That's it. Our class would look something like this:
(code is simplified for brevity)
*/

class Host : public Retrievable {
public:
	Host() : Retrievable("Hosts") {} // Name of the table has to match that in the database
	int GetAsset() { return asset; }
	void SetAsset(int value) { asset = value; }
	double GetPrice() { return price; }
	void SetPrice(double value) { price = value; }

	int64_t host_id = 0;
	std::string hostName;
	char *addressIP = nullptr;
private:
	virtual void Initialize() {
		RegisterField("host_id", &host_id); // the same goes for the column name - it has to match the one in the database schema
		RegisterField("host_name", &hostName);
		RegisterField("address_ip", &addressIP);
		RegisterField("asset", &asset);
		RegisterField("price", &price);
	}
private:
	int asset = 0;
	double price = 0;
};


/*If we'd like our class (table) to contain a foreign key we have to create a filed of the appropriate type and register it with the RegisterField<ReferredType>()*/

class Service : public Retrievable {
public:
	Service() : Retrievable("Services") {}
	int service_id = 0;
	int64_t host_id = 0;
	std::string name;
	uint32_t port = 0;
	tm start_date;
private:
	virtual void Initialize() {
		RegisterField("service_id", &service_id);
		RegisterField<Host>("host_id", &host_id); // Foreign Key to the Host class (table). The column name doesn't have to match the name of the PK column in the other table
		RegisterField("name", &name);
		RegisterField("port", &port);
		RegisterField("start_date", &start_date);
	}
};


/*If a relation is to be of the one-to-one(zero) type then we register the Foreign Key in place of the Primary Key (at the first place)*/

class One2one : public Retrievable {
public:
	One2one() : Retrievable("One2ones") {}
	int host_id = 0;
	std::string desc;

	void Test() {}
private:
	virtual void Initialize() {
		RegisterField<Host>("hostID", &host_id); // Foreign Key
		RegisterField("description", &desc);
	}
};


/*Classes (tables) can have Primary Keys of types other than the integer. However in that case they won't be auto-incremented by the database and they always need to have value and be unique during writing to the database*/

class StringKey: public Retrievable {
public:
	StringKey() : Retrievable("StringKeys") {}
	std::string sk_id;
	std::string desc;

	void Test() {}
private:
	virtual void Initialize() {
		RegisterField("sk_id", &sk_id);
		RegisterField("description", &desc);
	}
};


int main() {

	// We're creating an object of the Host class and filling it with sample data
	Host host1;

	host1.hostName = "Internal server";
	host1.addressIP = new char[50];
	strcpy(host1.addressIP, "192.168.1.88");
	host1.SetAsset(666);
	host1.SetPrice(9999.99);
	//host1.host_id = ... leaving the zero for the PK value we're informing the database to set the Id automatically

	// Retrievable objects are 'copyable' and 'assignable'
	Host host2;
	host2 = host1;
	assert(host2.GetAsset() == 666);

	// creating a DbService object to operate on the database
	DbService db; // if we use the default constructor it will use "./storm.db" as the database

	// if we have compiled the STORM library with the `DB_INIT` flag (`make dbinit`) then the library will take responsibility for creating the database file and/or for updating the database schema

	// saving a single entity to the database
	db.SaveEntity(host2);

	// after saving the entity it was filled with the auto-incremented Id value that the database has assigned to it
	assert(host2.host_id);

	// updating the entity
	host2.hostName.assign("External server ONE");
	db.UpdateEntity(host2);

	// deleting an antity is as easy
	db.DeleteEntity(host2); // this way or with an empty object:
	/*
	Host emptyHost2;
	emptyHost2.host_id = host2.host_id;
	db.DeleteEntity(emptyHost2);
	*/

	// let's write it again so it could be yet useful
	host2.host_id = 0;
	db.SaveEntity(host2);

	// creating another Host object
	Host host3;

	time_t rawtime1;
	time (&rawtime1);
	host3.host_id = rawtime1; // this time we are setting the Id value manually. It'll be used as the Primary Key in the database (we need some unique number for this example to be able to run more that once)
	host3.hostName = "Super duper cloud computer";
	host3.SetAsset(42);

	// next we're creating sample services connected to the host2 object by the Foreign Key
	Service service1;
	service1.host_id = host2.host_id;
	service1.name = "web server";
	service1.port = 80;
	time_t t(time(0));
	tm start_d = *gmtime(&t);
	service1.start_date = start_d;

	Service service2;
	service2.host_id = host2.host_id;
	service2.name = "web API";
	t = time(0);
	start_d = *gmtime(&t);
	start_d.tm_year -= 2;
	service2.start_date = start_d;

	// now we can write all the new various Retrievable objects to the database at once. We just need to put the pointers to them in a std::vector
	std::vector<Retrievable*> ourEntities = { &host3, &service1, &service2 };

	// this is an atomic transaction so all of them will be written or none of them
	db.SaveEntities(ourEntities);

	// to illustrate even more how transactions work we will create a few more entities of which the last one will be invalid (will have the same Id as the second one)
	Host h1, h2, h3;
	h1.hostName = "zzzzzzzzzzzzzzzzzzzz";
	h2.hostName = "zzzzzzzzzzzzzzzzzzzz";
	h3.hostName = "zzzzzzzzzzzzzzzzzzzz";
	h2.host_id = h3.host_id = 9999;
	std::vector<Retrievable*> ourInvalidEntities = { &h1, &h2, &h3 };

	try {
		db.SaveEntities(ourInvalidEntities);
		assert(0);
	} catch(...) {}

	// we can check if any of the above entities was written to the database. For this purpose we're using the RetrieveEntities function that takes predicate
	std::vector<Host> result(db.RetrieveEntities<Host>("host_name LIKE '%zzzz%'"));
	assert(!result.size());

	// and to compare
	std::vector<Service>result2(db.RetrieveEntities<Service>("name LIKE 'web%'"));
	assert(result2.size() >= 2);

	// also trying to write with an invalid FK will fail (that is with an FK linking to a non-existent parent)
	Service invalid_s;
	invalid_s.host_id = 999999;
	try {
		db.SaveEntity(invalid_s);
		assert(0);
	} catch(...) {}

	// the primary way to get one record (entity) from a database is to "hydrate" a Retrievable object which has a non-empty Id field
	Service service3;
	service3.service_id = service1.service_id;
	db.RetrieveEntity(service3);
	assert(service3.name == "web server");
	assert(service3.port == 80);

	// we can also retrieve a collection of child entities (related to the parent by means of the Foreign Key)
	std::vector<Service> childEntities = db.RetrieveChildEntities<Service>(host2);
	assert(childEntities.size() == 2);

	// an example with the one-to-one(zero) relation
	One2one o2o1;
	o2o1.host_id = host3.host_id;
	o2o1.desc = "I'm your only child";

	db.SaveEntity(o2o1);

	// an example with a string key
	StringKey strK;
	time_t rawtime2;
	time (&rawtime2);
	std::string stime = ctime (&rawtime2);
	strK.sk_id = stime.substr(0, stime.length() - 1);
	strK.desc = "my key must be unique";

	db.SaveEntity(strK);

	// there is also possibility to set a value on a Retrievable in a more flexible way using the std:string
	Retrievable *some_entity = &o2o1;
	some_entity->SetStrValue("description", "new value");

	assert(o2o1.desc == "new value");

	// and get it as the std::string
	std::string value = service3.GetStrValue("port");
	assert(value == "80");

	// of course it's not limited to string fields
	host3.SetStrValue("price", "99.9");
	assert(host3.GetPrice() == 99.9);

	// an additional feature is ability to serialize Retrievable objects to the JSON format
	std::cout << "\nSTARRING:\n\n";
	std::cout << "host2: " << retrievable2JSON(host2);
	std::cout << "\n\nhost3: " << retrievable2JSON(host3);
	std::cout << "\n\nservice1: " <<	retrievable2JSON(service1);
	std::cout << "\n\nservice2: " << retrievable2JSON(service2);
	std::cout << "\n\nservice3: " << retrievable2JSON(service3);
	std::cout << "\n\nchild entities:\n";

	for (Service &child: childEntities) {
		std::cout << "	- " << retrievable2JSON(child) << "\n";
	}
	std::cout << "\no2o1: " << retrievable2JSON(o2o1) << "\n";
	std::cout << "\nstrK: " << retrievable2JSON(strK) << std::endl;
}

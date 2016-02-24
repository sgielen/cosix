#include <oslibc/list.hpp>
#include <catch.hpp>

struct body {
	body(int v) : value(v) {}
	int value;
};

TEST_CASE("empty") {
	linked_list<body*> *list = nullptr;
	REQUIRE(empty(list) == true);
	list = new linked_list<body*>();
	list->data = nullptr;
	list->next = nullptr;
	REQUIRE(empty(list) == false);
	list->data = new body(5);
	list->next = list; /* normally this is never allowed, but empty() must not even access this member */
	REQUIRE(empty(list) == false);
	delete list->data;
	delete list;
}

TEST_CASE("size") {
	linked_list<body*> *list = nullptr;
	REQUIRE(size(list) == 0);
	list = new linked_list<body*>();
	list->data = nullptr;
	list->next = nullptr;
	REQUIRE(size(list) == 1);
	list->data = new body(5);
	list->next = new linked_list<body*>();
	list->next->data = nullptr;
	list->next->next = nullptr;
	REQUIRE(size(list) == 2);
	delete list->next;
	delete list->data;
	delete list;
}

TEST_CASE("iterate") {
	linked_list<body*> *list = nullptr;

	iterate(list, [](linked_list<body*>*) {
		FAIL("Lambda called by iterate() while list is empty");
	});

	list = new linked_list<body*>();
	list->data = new body(23);
	list->next = new linked_list<body*>();
	list->next->data = new body(12);
	list->next->next = new linked_list<body*>();
	list->next->next->data = new body(45);
	list->next->next->next = nullptr;

	std::vector<int> values_seen;
	iterate(list, [&values_seen](linked_list<body*> *item) {
		values_seen.push_back(item->data->value);
	});

	REQUIRE(values_seen.size() == 3);
	REQUIRE(values_seen[0] == 23);
	REQUIRE(values_seen[1] == 12);
	REQUIRE(values_seen[2] == 45);
}

TEST_CASE("find") {
	linked_list<body*> *list = nullptr;
	REQUIRE(find(list, [](linked_list<body*>*){
		FAIL("Lambda called by find() while list is empty");
		return true;
	}) == nullptr);

	list = new linked_list<body*>();
	list->data = nullptr;
	list->next = nullptr;

	bool called = false;
	REQUIRE(find(list, [&called, list](linked_list<body*> *item){
		REQUIRE(called == false);
		REQUIRE(item == list);
		called = true;
		return false;
	}) == nullptr);
	REQUIRE(called == true);

	called = false;
	REQUIRE(find(list, [&called, list](linked_list<body*> *item){
		REQUIRE(called == false);
		REQUIRE(item == list);
		called = true;
		return true;
	}) == list);
	REQUIRE(called == true);

	list->data = new body(678);
	list->next = new linked_list<body*>();
	list->next->data = new body(987);
	list->next->next = nullptr;
	std::vector<int> values_seen;
	REQUIRE(find(list, [&values_seen](linked_list<body*> *item) {
		values_seen.push_back(item->data->value);
		return false;
	}) == nullptr);
	REQUIRE(values_seen.size() == 2);
	REQUIRE(values_seen[0] == 678);
	REQUIRE(values_seen[1] == 987);

	values_seen.clear();
	REQUIRE(find(list, [&values_seen](linked_list<body*> *item) {
		values_seen.push_back(item->data->value);
		return item->data->value == 678;
	}) == list);
	REQUIRE(values_seen.size() == 1);
	REQUIRE(values_seen[0] == 678);

	values_seen.clear();
	REQUIRE(find(list, [&values_seen](linked_list<body*> *item) {
		values_seen.push_back(item->data->value);
		return item->data->value == 987;
	}) == list->next);
	REQUIRE(values_seen.size() == 2);
	REQUIRE(values_seen[0] == 678);
	REQUIRE(values_seen[1] == 987);

	delete list->next->data;
	delete list->next;
	delete list->data;
	delete list;
}

/* contains and contains_object are implemented in terms of find, so we don't have to test them */

TEST_CASE("append") {
	INFO("a");
	linked_list<body*> *list = nullptr;
	REQUIRE(empty(list) == true);
	REQUIRE(size(list) == 0);

	INFO("b");
	linked_list<body*> *entry = new linked_list<body*>();
	entry->data = nullptr;
	entry->next = nullptr;

	append(&list, entry);
	INFO("c");
	REQUIRE(list == entry);
	REQUIRE(entry->data == nullptr);
	REQUIRE(entry->next == nullptr);
	REQUIRE(empty(list) == false);
	REQUIRE(size(list) == 1);

	linked_list<body*> *entry2 = new linked_list<body*>();
	linked_list<body*> *entry3 = new linked_list<body*>();
	entry2->data = nullptr;
	entry2->next = entry3;
	entry3->data = nullptr;
	entry3->next = nullptr;
	INFO("d");
	append(&list, entry2);
	INFO("e");
	REQUIRE(list == entry);
	REQUIRE(list->next == entry2);
	REQUIRE(list->next->next == entry3);
	REQUIRE(list->next->next->next == nullptr);
	REQUIRE(empty(list) == false);
	REQUIRE(size(list) == 3);

	INFO("f");
	delete entry3;
	delete entry2;
	delete entry;
}

TEST_CASE("remove_one_first") {
	linked_list<body*> *list = nullptr;
	REQUIRE(remove_one(&list, [](linked_list<body*>*){
		FAIL("Functor called on empty list");
		return true;
	}, [](body*) {
		FAIL("Deallocator called on empty list");
	}) == false);

	list = new linked_list<body*>();
	linked_list<body*> *orig_list = list;
	list->data = nullptr;
	list->next = nullptr;
	bool called = false;
	REQUIRE(remove_one(&list, [&called, list](linked_list<body*> *item){
		REQUIRE(called == false);
		REQUIRE(item == list);
		called = true;
		return false;
	}, [](body*) {
		FAIL("Deallocator called wrongly");
	}) == false);
	REQUIRE(called == true);
	REQUIRE(list == orig_list);
	REQUIRE(list->next == nullptr);

	called = false;
	bool deallocator_called = false;
	body *b1 = new body(345);
	list->data = b1;

	REQUIRE(remove_one(&list, [&called, list](linked_list<body*> *item){
		REQUIRE(called == false);
		REQUIRE(item == list);
		called = true;
		return true;
	}, [&deallocator_called, list](body *b) {
		REQUIRE(deallocator_called == false);
		REQUIRE(b == list->data);
		deallocator_called = true;
	}) == true);
	/* the only item was removed */
	REQUIRE(list == nullptr);

	delete b1;
	delete orig_list;
}

TEST_CASE("remove_one_second") {
	linked_list<body*> *l1 = new linked_list<body*>();
	linked_list<body*> *l2 = new linked_list<body*>();
	linked_list<body*> *l3 = new linked_list<body*>();
	linked_list<body*> *l4 = new linked_list<body*>();

	body *b1 = new body(22);
	body *b2 = new body(33);
	body *b3 = new body(44);
	body *b4 = new body(11);

	linked_list<body*> *list = l1;
	list->data = b1;
	list->next = l2;
	list->next->data = b2;
	list->next->next = l3;
	list->next->next->data = b3;
	list->next->next->next = l4;
	list->next->next->next->data = b4;
	list->next->next->next->next = nullptr;

	std::vector<int> values_seen;
	std::vector<int> values_destructed;
	REQUIRE(remove_one(&list, [&values_seen](linked_list<body*> *item) {
		values_seen.push_back(item->data->value);
		return item->data->value > 30;
	}, [&values_destructed](body *data) {
		values_destructed.push_back(data->value);
	}) == true);
	REQUIRE(values_seen.size() == 2);
	REQUIRE(values_seen[0] == 22);
	REQUIRE(values_seen[1] == 33);
	REQUIRE(values_destructed.size() == 1);
	REQUIRE(values_destructed[0] == 33);
	REQUIRE(list == l1);
	REQUIRE(list->data == b1);
	REQUIRE(list->next == l3);
	REQUIRE(list->next->data == b3);
	REQUIRE(list->next->next == l4);
	REQUIRE(list->next->next->data == b4);
	REQUIRE(list->next->next->next == nullptr);

	delete b4;
	delete b3;
	delete b2;
	delete b1;

	delete l4;
	delete l3;
	delete l2;
	delete l1;
}

/* remove_object is implemented in terms of remove_one, so we don't have to test it */

TEST_CASE("remove_all") {
	linked_list<body*> *l1 = new linked_list<body*>();
	linked_list<body*> *l2 = new linked_list<body*>();
	linked_list<body*> *l3 = new linked_list<body*>();
	linked_list<body*> *l4 = new linked_list<body*>();
	linked_list<body*> *l5 = new linked_list<body*>();

	body *b1 = new body(22);
	body *b2 = new body(33);
	body *b3 = new body(44);
	body *b4 = new body(11);
	body *b5 = new body(55);

	linked_list<body*> *list = l1;
	list->data = b1;
	list->next = l2;
	list->next->data = b2;
	list->next->next = l3;
	list->next->next->data = b3;
	list->next->next->next = l4;
	list->next->next->next->data = b4;
	list->next->next->next->next = l5;
	list->next->next->next->next->data = b5;
	list->next->next->next->next->next = nullptr;

	std::vector<int> values_seen;
	std::vector<int> values_destructed;
	REQUIRE(remove_all(&list, [&values_seen](linked_list<body*> *item) {
		values_seen.push_back(item->data->value);
		return item->data->value > 30;
	}, [&values_destructed](linked_list<body*> *item) {
		values_destructed.push_back(item->data->value);
	}) == 3);
	REQUIRE(values_seen.size() == 5);
	REQUIRE(values_seen[0] == 22);
	REQUIRE(values_seen[1] == 33);
	REQUIRE(values_seen[2] == 44);
	REQUIRE(values_seen[3] == 11);
	REQUIRE(values_seen[4] == 55);
	REQUIRE(values_destructed.size() == 3);
	REQUIRE(values_destructed[0] == 33);
	REQUIRE(values_destructed[1] == 44);
	REQUIRE(values_destructed[2] == 55);
	REQUIRE(list == l1);
	REQUIRE(list->data == b1);
	REQUIRE(list->next == l4);
	REQUIRE(list->next->data == b4);
	REQUIRE(list->next->next == nullptr);

	delete b5;
	delete b4;
	delete b3;
	delete b2;
	delete b1;

	delete l5;
	delete l4;
	delete l3;
	delete l2;
	delete l1;
}

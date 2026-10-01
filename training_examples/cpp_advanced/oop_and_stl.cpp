/*
 * oop_and_stl.cpp - the advanced C++ part of the syllabus in one file.
 *
 * Covered: classes, encapsulation, constructors/destructors, inheritance,
 * single and multiple and multilevel inheritance, virtual functions, pure
 * virtual functions and abstract classes, RTTI and dynamic_cast, static
 * members, friend functions, function and class templates, new/delete,
 * the three smart pointers, move semantics, and the STL algorithms.
 *
 * Build:  make
 * Run:    ./oop_and_stl
 */
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace {

void section(const char* title)
{
	std::cout << "\n=== " << title << " ===\n";
}

/* ---------------- 1. encapsulation + static members ---------------- */
class ReadingCounter {
public:
	explicit ReadingCounter(const std::string& name) : name_(name) {}

	void add_reading()
	{
		++count_;
	}

	/* A static member belongs to the class, not to an object: one counter
	 * shared by every instance. */
	static long total_readings() { return count_; }

	const std::string& name() const { return name_; }

private:
	std::string name_;
	static long count_; /* one copy for the whole class */
};

long ReadingCounter::count_ = 0; /* definition of the static member */

/* ---------------- 2. friend function ---------------- */
class Thermometer {
public:
	explicit Thermometer(double celsius) : celsius_(celsius) {}
	double celsius() const { return celsius_; }

	/* A friend is not a member, so it has no `this`. It is granted access
	 * to the private state, which is useful for tight operator overloads
	 * and for printing. */
	friend double to_fahrenheit(const Thermometer& t);

private:
	double celsius_;
};

double to_fahrenheit(const Thermometer& t)
{
	return t.celsius_ * 9.0 / 5.0 + 32.0;
}

/* ---------------- 3. base, virtual and pure virtual ---------------- */
class Sensor {
public:
	virtual ~Sensor() = default; /* virtual destructor: mandatory here */

	/* A pure virtual function makes the class abstract: it cannot be
	 * instantiated, so every subclass is forced to provide the behaviour.
	 * This is the "interface" idea the parking monitor relies on. */
	virtual int read_cm() const = 0;
	virtual const char* kind() const = 0;
};

class UltrasonicSensor : public Sensor {
public:
	explicit UltrasonicSensor(int cm) : cm_(cm) {}
	int read_cm() const override { return cm_; }
	const char* kind() const override { return "ultrasonic"; }

private:
	int cm_;
};

class LaserSensor : public Sensor {
public:
	explicit LaserSensor(int cm) : cm_(cm) {}
	int read_cm() const override { return cm_; }
	const char* kind() const override { return "laser"; }

private:
	int cm_;
};

/* ---------------- 4. multilevel inheritance ---------------- */
class Vehicle {
public:
	virtual const char* type() const { return "vehicle"; }
	virtual ~Vehicle() = default;
};

class Car : public Vehicle {
public:
	const char* type() const override { return "car"; }
};

class SportsCar : public Car {
public:
	const char* type() const override { return "sports car"; }
};

/* ---------------- 5. multiple inheritance ---------------- */
class HasSpeedometer {
public:
	virtual ~HasSpeedometer() = default;
	int speed_kmh() const { return 120; }
};

class Electric {
public:
	virtual ~Electric() = default;
	const char* fuel() const { return "electric"; }
};

class SportsCarHybrid : public SportsCar, public HasSpeedometer, public Electric {
public:
	/* One destructor overrides all the base destructors. */
	~SportsCarHybrid() override = default;
};

/* ---------------- 6. RTTI and dynamic_cast ---------------- */
void demo_rtti()
{
	section("6. RTTI and dynamic_cast");

	std::vector<std::unique_ptr<Sensor>> sensors;
	sensors.push_back(std::make_unique<UltrasonicSensor>(37));
	sensors.push_back(std::make_unique<LaserSensor>(91));

	for (const auto& s : sensors) {
		std::cout << "  kind() = " << s->kind() << ", read_cm() = "
			  << s->read_cm() << "\n";

		/* dynamic_cast asks what the object really is at run time. */
		if (auto* ultra =
			    dynamic_cast<UltrasonicSensor*>(s.get())) {
			std::cout << "    dynamic_cast succeeded: this really "
				     "is an UltrasonicSensor\n";
			(void)ultra;
		}

		/* typeid is the other half of RTTI. */
		std::cout << "    typeid name = " << typeid(*s).name() << "\n";
	}
}

/* ---------------- 7. virtual dispatch vs static type ---------------- */
void demo_virtual_dispatch()
{
	section("7. Virtual dispatch through a base pointer");

	Sensor* s = new UltrasonicSensor(45); /* base pointer, derived object */
	std::cout << "  through Sensor*:      " << s->kind() << " "
		  << s->read_cm() << " cm\n";
	delete s;

	Vehicle* v = new SportsCar();
	std::cout << "  SportsCar as Vehicle*: " << v->type() << "\n";
	delete v;

	/* The same static type, two behaviours - that is the whole point of
	 * virtual functions and of the StatePolicy interface in the app. */
}

/* ---------------- 8. templates ---------------- */
template <typename T>
class Stack {
public:
	void push(const T& value) { data_.push_back(value); }

	T pop()
	{
		if (data_.empty())
			throw std::underflow_error("stack is empty");
		T value = data_.back();
		data_.pop_back();
		return value;
	}

	size_t size() const { return data_.size(); }

private:
	std::vector<T> data_;
};

template <typename T>
T maximum(const T& a, const T& b)
{
	return a > b ? a : b;
}

void demo_templates()
{
	section("8. Function and class templates");

	Stack<int> ints;
	ints.push(1);
	ints.push(2);
	ints.push(3);
	std::cout << "  Stack<int> top pop = " << ints.pop() << ", size = "
		  << ints.size() << "\n";

	Stack<std::string> strings;
	strings.push("parking");
	strings.push("sensor");
	std::cout << "  Stack<string> pop = " << strings.pop() << "\n";

	std::cout << "  maximum(3, 9) = " << maximum(3, 9) << "\n";
	const std::string word_a = "a", word_z = "z";
	std::cout << "  maximum(\"a\", \"z\") = " << maximum(word_a, word_z)
		  << "\n";
	std::cout << "  one implementation, any type - that is the point of\n"
		  << "  templates, and the STL is built entirely from them.\n";
}

/* ---------------- 9. new / delete ---------------- */
class DynamicBuffer {
public:
	explicit DynamicBuffer(std::size_t count) : data_(new int[count])
	{
		std::fill(data_, data_ + count, 0);
		std::cout << "  new int[" << count << "] at " << data_ << "\n";
	}

	~DynamicBuffer()
	{
		delete[] data_; /* the [] must match the new[] */
		std::cout << "  delete[] done\n";
	}

	void set(std::size_t i, int v) { data_[i] = v; }
	int get(std::size_t i) const { return data_[i]; }

	/* Copying a raw owning pointer would double free, so the copy
	 * operations are deleted. smart_ptr does the right thing instead. */
	DynamicBuffer(const DynamicBuffer&) = delete;
	DynamicBuffer& operator=(const DynamicBuffer&) = delete;

private:
	int* data_;
};

void demo_new_delete()
{
	section("9. new and delete, and why they are risky");

	{
		DynamicBuffer buf(4);
		buf.set(0, 42);
		std::cout << "  buf.get(0) = " << buf.get(0) << "\n";
	} /* the destructor frees the block even on an early return */

	std::cout << "  the project itself avoids raw new/delete: the driver\n"
		  << "  state is static, and the app uses containers and\n"
		  << "  smart_ptr. This module shows the mechanism anyway.\n";
}

/* ---------------- 10. smart pointers ---------------- */
void demo_smart_pointers()
{
	section("10. unique_ptr, shared_ptr and weak_ptr");

	/* unique_ptr: exactly one owner. The parking monitor uses this for
	 * its services, because only the monitor may destroy them. */
	std::unique_ptr<Sensor> owner = std::make_unique<UltrasonicSensor>(30);
	std::cout << "  unique_ptr -> " << owner->kind() << " "
		  << owner->read_cm() << " cm\n";

	/* shared_ptr: several owners, reference counted. */
	auto shared_a = std::make_shared<UltrasonicSensor>(55);
	std::weak_ptr<Sensor> weak = shared_a; /* observes without owning */
	std::cout << "  shared_ptr use_count = " << shared_a.use_count()
		  << " (the weak_ptr does not count)\n";
	{
		auto shared_b = shared_a;
		std::cout << "  after a second owner, use_count = "
			  << shared_a.use_count() << "\n";
	}
	std::cout << "  use_count is back to " << shared_a.use_count() << "\n";

	/* The point of weak_ptr: it breaks the reference cycle that
	 * shared_ptr alone cannot handle. */
	{
		auto cycle = std::make_shared<std::vector<std::shared_ptr<int>>>();
		std::shared_ptr<int> element = std::make_shared<int>(1);
		cycle->push_back(element);
		element.reset(); /* drop this owner */
		std::cout << "  vector still holds " << cycle->size()
			  << " element; a weak_ptr could inspect it without\n"
			  << "  keeping it alive\n";
	}

	/* expired() is how a weak_ptr says "nobody owns it any more". */
	shared_a.reset();
	std::cout << "  weak_ptr.expired() = " << std::boolalpha
		  << weak.expired() << "\n";
}

/* ---------------- 11. move semantics ---------------- */
class Message {
public:
	Message(std::string text) : text_(std::move(text)) {}
	Message(const Message&) = default;
	Message& operator=(const Message&) = default;
	Message(Message&&) noexcept = default;
	Message& operator=(Message&&) noexcept = default;

	const std::string& text() const { return text_; }

private:
	std::string text_;
};

void demo_move_semantics()
{
	section("11. Copy vs move");

	Message a("distance=42");
	Message b = a; /* copy: two independent strings */
	Message c = std::move(a); /* move: c takes the buffer, a is empty */

	std::cout << "  b.text() = " << b.text() << "\n";
	std::cout << "  c.text() = " << c.text() << "\n";
	std::cout << "  a.text() = \"" << a.text()
		  << "\" (moved from - valid but unspecified)\n";
}

/* ---------------- 12. STL algorithms ---------------- */
struct Reading {
	int distance_cm;
	std::string state;
};

void demo_algorithms()
{
	section("12. STL algorithms over a container of readings");

	std::vector<Reading> readings = {
		{150, "SAFE"}, {60, "SAFE"},   {50, "WARNING"},
		{30, "WARNING"}, {20, "DANGER"}, {10, "DANGER"},
	};

	/* sort + lambda comparator */
	std::sort(readings.begin(), readings.end(),
		  [](const Reading& a, const Reading& b) {
			  return a.distance_cm < b.distance_cm;
		  });
	std::cout << "  sorted by distance: ";
	for (const auto& r : readings)
		std::cout << r.distance_cm << " ";
	std::cout << "\n";

	/* find_if: the first reading in a dangerous state */
	auto danger = std::find_if(readings.begin(), readings.end(),
				   [](const Reading& r) {
					   return r.state == "DANGER";
				   });
	if (danger != readings.end())
		std::cout << "  first DANGER at " << danger->distance_cm << " cm\n";

	/* count_if: how many readings are in the warning band */
	auto warnings = std::count_if(
		readings.begin(), readings.end(), [](const Reading& r) {
			return r.state == "WARNING";
		});
	std::cout << "  WARNING readings = " << warnings << "\n";

	/* transform + accumulate: the closest reading */
	int closest = std::accumulate(
		readings.begin(), readings.end(), readings[0].distance_cm,
		[](int acc, const Reading& r) {
			return std::min(acc, r.distance_cm);
		});
	std::cout << "  closest reading = " << closest << " cm\n";

	/* map: group by state, which is what the SRS asks for */
	std::map<std::string, int> perState;
	for (const auto& r : readings)
		++perState[r.state];
	std::cout << "  counts: ";
	for (const auto& entry : perState)
		std::cout << entry.first << "=" << entry.second << " ";
	std::cout << "\n";

	/* remove_if + erase: drop everything that is not interesting */
	std::vector<Reading> onlyDanger;
	std::copy_if(readings.begin(), readings.end(),
		     std::back_inserter(onlyDanger),
		     [](const Reading& r) { return r.state == "DANGER"; });
	std::cout << "  copy_if kept " << onlyDanger.size() << " DANGER readings\n";
}

} // namespace

int main()
{
	std::cout << "Advanced C++ - training demonstration\n";
	std::cout << "(not part of the parking monitor runtime)\n";

	/* 1. static members */
	section("1. Encapsulation, constructors and static members");
	ReadingCounter rc("ultrasonic");
	rc.add_reading();
	rc.add_reading();
	std::cout << "  " << rc.name() << " total readings across all objects: "
		  << ReadingCounter::total_readings() << "\n";

	/* 2. friend */
	section("2. Friend function");
	Thermometer t(37.0);
	std::cout << "  37 C = " << to_fahrenheit(t) << " F\n";

	/* 3-5. inheritance */
	section("3-5. Single, multilevel and multiple inheritance");
	UltrasonicSensor us(88);
	Sensor* asBase = &us;
	std::cout << "  through the base class: " << asBase->kind() << " "
		  << asBase->read_cm() << " cm\n";

	SportsCar sc;
	std::cout << "  SportsCar as Vehicle:  " << sc.type() << "\n";
	std::cout << "  SportsCarHybrid:      " << sc.type() << ", "
		  << sc.speed_kmh() << " km/h, " << sc.fuel() << "\n";

	demo_rtti();
	demo_virtual_dispatch();
	demo_templates();
	demo_new_delete();
	demo_smart_pointers();
	demo_move_semantics();
	demo_algorithms();

	std::cout << "\nAll advanced C++ demonstrations completed.\n";
	return 0;
}

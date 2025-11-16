#include"HS_Leak.h"
#include<iostream>

static void Leak1()
{
	for (int i = 0; i < 100; i++)
		new int;
}

static void Leak2()
{
	for (int i = 0; i < 100; i++)
		new char;
}

static void Leak3()
{
	for (int i = 0; i < 100; i++)
		new double;
}

int main()
{
	HSLL::Utils::MemoryTracer::StartTracing();
	Leak1();
	Leak2();
	Leak3();
	std::cout << HSLL::Utils::MemoryTracer::EndTracing() << std::endl;
	return 0;
}
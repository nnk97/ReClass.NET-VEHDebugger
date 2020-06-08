// Example test application.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

//#pragma optimize("", off)

#include <iostream>
#include <Windows.h>

std::uintptr_t AddyForSomeValue;

void function1()
{
    std::cout << __FUNCTION__ << " > Some value: " << *(DWORD*)(AddyForSomeValue) << std::endl;
}

void function2()
{
    std::cout << __FUNCTION__ << " > Some value: " << *(DWORD*)(AddyForSomeValue) << std::endl;
}

void function3()
{
    *(DWORD*)(AddyForSomeValue) = rand() % 100;
    std::cout << __FUNCTION__ << " > Some value: " << *(DWORD*)(AddyForSomeValue) << std::endl;
}

int main()
{
    AddyForSomeValue = (std::uintptr_t)VirtualAlloc(0, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) + 10;

    *(DWORD*)(AddyForSomeValue) = rand() % 100;
    std::cout << "\tSome value = " << *(DWORD*)(AddyForSomeValue) << std::endl;
    std::cout << "\tSome value @ 0x" << std::hex << AddyForSomeValue << std::endl << std::dec << std::endl;
    std::cout << "\tMenu:" << std::endl;
    std::cout << "\t 1. Access variable from fucntion 1" << std::endl;
    std::cout << "\t 2. Access variable from fucntion 2" << std::endl;
    std::cout << "\t 3. Change variable from fucntion 3" << std::endl;

    while (true)
    {
        int Choice;
        std::cin >> Choice;

        if (Choice == 1)
            function1();
        else if (Choice == 2)
            function2();
        else if (Choice == 3)
            function3();
    }
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

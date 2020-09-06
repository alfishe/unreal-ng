#ifndef MESSAGE_CENTER_STREAMHELPER_H
#define MESSAGE_CENTER_STREAMHELPER_H

#include <iostream>
#include <iomanip>

namespace mc_function_display
{
    // Templated helper to print function pointers
    // See: https://stackoverflow.com/questions/2064692/how-to-print-function-pointers-with-cout
    // Example code:
    //    void fun_void_void(){};
    //    void fun_void_double(double d){};
    //    double fun_double_double(double d){return d;}
    //
    //    int main(){
    //        using namespace function_display;
    //        // ampersands & are optional
    //        std::cout << "1. " << &fun_void_void << std::endl; // prints "1. funptr 0x40cb58"
    //        std::cout << "2. " << &fun_void_double << std::endl; // prints "2. funptr 0x40cb5e"
    //        std::cout << "3. " << &fun_double_double << std::endl; // prints "3. funptr 0x40cb69"
    //    }
    template<class Ret, class... Args>
    std::ostream& operator <<(std::ostream& os, Ret(*p)(Args...) )
    {
        return os << "funptr " << (void*)p;
    }
}

#include <functional>
namespace mc_lambda_display
{
    template<typename T, typename... U>
    size_t getTargetAddress(std::function<T(U...)> func)
    {
        return (size_t) *(long *)(char *)&func;
    }

    // Templated helper to print function wrapped blocks (including lambdas)
    // See: https://stackoverflow.com/questions/18039723/c-trying-to-get-function-address-from-a-stdfunction
    // Example:
    // auto lambda = [](int i, int j) { std::cout << i + j; }
    // std::cout << lambda;
    template<typename R, class... Args>
    std::ostream& operator <<(std::ostream& os, std::function<R(Args...)>p)
    {
        auto fp = getTargetAddress(p);
        return os << "lambdaptr " << fp;
    }
}

namespace mc_class_method_display
{
    // Templated helper to print class method pointers
    // See: https://stackoverflow.com/questions/11111969/how-to-print-member-function-address-in-c
    // Example:
    // std::cout << &Class::Method; // prints clsmtdptr 0x8cbacd000
    template<typename R, typename T, class... Args>
    std::ostream& operator <<(std::ostream& os, R(T::*p)(Args...) )
    {
        void* ptr = static_cast<void*>(&p);
        return os  << "clsmtdptr " << ptr;

        /*
        // Conversion via union and then re-printing pointer
        union PointerConversion
        {
            R(T::*f)(Args...);
            std::array<unsigned char, sizeof(p)> buf;
        } pc;

        pc.f = p;

        os << "cls mtd ";
        os << std::hex << std::setfill('0') << "0x";

        // TODO: Should be traversed in reverse order for LSB architectures
        for (auto c : pc.buf)
            os << std::setw(2) << (unsigned)c;

        return os;
        */
    }
}

#endif //MESSAGE_CENTER_STREAMHELPER_H

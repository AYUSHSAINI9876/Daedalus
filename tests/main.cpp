// Entry point shared by every Daedalus test translation unit.
#include "framework/TestFramework.hpp"

int main(int argc, char** argv) {
    return ::daedalus::testing::Registry::instance().run(argc, argv);
}

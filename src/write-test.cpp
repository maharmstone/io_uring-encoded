#include <iostream>

using namespace std;

static void do_ioctl_tests() {
    // FIXME
}

int main() {
    try {
        do_ioctl_tests();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}

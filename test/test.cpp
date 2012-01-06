
#include "../floppyIO.h";

int main() {
    try {
    
        FloppyIO fio ("./test.fp", F_SYNCHRONIZED | F_EXCEPTIONS );
        fio.syncTimeout=5;
        string str;
        int szBytes;
        
        while (1) {

            printf("Waiting for input...");
            fflush(stdout);
            szBytes = fio.receive(&str);        
            if (szBytes<0) {
                printf("failed!\n%s\nError = %i\n", fio.errorStr.c_str(), szBytes);
                return szBytes;
            }

            printf("ok\nGot '%s' (%i bytes)\nSending it back...", str.c_str(), szBytes);
            fflush(stdout);
            szBytes = fio.send(str);
            if (szBytes<0) {
                printf("failed!\nError = %i\n", szBytes);
                return szBytes;
            }

            printf("ok\nSent %i bytes\n", szBytes);

        }

    } catch (std::exception e) {
        cout << "\n** EXCEPTION (Type=" << typeid(e).name() << ")\n** Message: " << e.what() << endl;    

    }

    return 0;
}

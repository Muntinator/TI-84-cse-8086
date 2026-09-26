/* Munt386 -- automated test runner. */
#include "test_util.h"

int g_tests = 0;
int g_fails = 0;

void test_memory(void);
void test_cpu(void);
void test_flags(void);
void test_strings(void);
void test_protected(void);
void test_bios(void);
void test_video(void);
void test_disk(void);
void test_pc(void);

int main(void)
{
    struct { const char *name; void (*fn)(void); } suites[] = {
        { "memory",    test_memory    },
        { "cpu",       test_cpu       },
        { "flags",     test_flags     },
        { "strings",   test_strings   },
        { "protected", test_protected },
        { "bios",      test_bios      },
        { "video",     test_video     },
        { "disk",      test_disk      },
        { "pc",        test_pc        },
    };

    printf("Munt386 test suite\n");
    printf("==================\n");
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        int before = g_tests;
        printf("[%s]\n", suites[i].name);
        suites[i].fn();
        printf("  %d checks\n", g_tests - before);
    }
    printf("==================\n");
    printf("%d checks, %d failures\n", g_tests, g_fails);
    return g_fails ? 1 : 0;
}

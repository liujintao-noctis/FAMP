#include <gtest/gtest.h>

#include <QApplication>
#include <QByteArray>

#include <cstring>

int main(int argc, char **argv)
{
    bool listTestsOnly = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--gtest_list_tests") == 0)
        {
            listTestsOnly = true;
            break;
        }
    }

    ::testing::InitGoogleTest(&argc, argv);
    if (listTestsOnly)
        return RUN_ALL_TESTS();

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
#if defined(Q_OS_WIN)
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
#else
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
#endif
    }
    QApplication application(argc, argv);
    return RUN_ALL_TESTS();
}

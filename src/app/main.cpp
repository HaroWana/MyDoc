#include <QApplication>
#include <QMainWindow>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("MonDoc"));
    window.resize(800, 600);
    window.show();
    QTimer::singleShot(0, &app, &QApplication::quit);
    return app.exec();
}

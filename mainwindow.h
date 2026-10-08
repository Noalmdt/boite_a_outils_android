#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_btnDetecter_clicked();
    void on_btnUnlock_clicked();
    void on_btnFlashRecovery_clicked();
    void on_btnFlashSystem_clicked();
    void on_btnDiagnostic_clicked();
    void on_btnSauvegarde_clicked();
    void on_btnSauvegardeTotale_clicked();

    void on_btnBrowseRecovery_clicked();

    void on_btnBrowseSystem_clicked();

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H

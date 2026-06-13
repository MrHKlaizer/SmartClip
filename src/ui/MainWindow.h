#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QPointer>
#include <QList>
#include <QPair>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QSet>
#include "SmartButton.h"

class Database;
class ImageViewer;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(Database *db, QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void showWindow();
    void markHistoryDirty();
    void pasteTextSilent(const QString &text); // вставить без открытия окна (хоткей профиля)
    void onUpdateAvailable(const QString &version, const QString &downloadUrl);

signals:
    void pasteRequested();
    void profilesChanged();                        // профили изменились → перерегистрировать хоткеи
    void mainHotkeyChanged(const QString &hotkey); // новая горячая клавиша из настроек
    void settingsChanged();                        // любые настройки сохранены

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    // ── Режим отображения ────────────────────────────────────────────────────
    enum class ViewMode { History, Pins };
    ViewMode m_viewMode      = ViewMode::History;
    QString  m_currentFolder = "";

    void switchView(ViewMode mode);

    // ── Режим мультивыбора закрепов ──────────────────────────────────────────
    bool      m_editMode       = false;
    QSet<int> m_selectedPinIds;

    void toggleEditMode();
    void showPinBatchContextMenu(const QPoint &globalPos);

    // ── Drag-to-reorder закрепов ─────────────────────────────────────────────
    class ClickableCard *m_dragCard   = nullptr;
    QVBoxLayout         *m_dragLayout = nullptr;

    void onCardDragMoved(const QPoint &globalPos);
    void onCardDragFinished();
    void savePinsColumnOrder(QVBoxLayout *layout);

    // ── Сортировка ───────────────────────────────────────────────────────────
    enum class SortOrder { DateDesc, DateAsc, NameAsc, NameDesc };
    SortOrder m_sortOrder = SortOrder::DateDesc;

    void showSortMenu();
    void applySortOrder(SortOrder order);
    QString sortLabel() const;

    // ── Очистка ──────────────────────────────────────────────────────────────
    void showClearMenu();
    void clearHistory(const QString &type);   // "" = все, "text", "image"
    void clearHistoryByApp();
    bool confirmTwice(const QString &title,
                      const QString &msg1,
                      const QString &msg2);   // два диалога подтверждения

    // ── Загрузка данных ──────────────────────────────────────────────────────
    void setupLayout();
    void loadHistory();
    void loadPins(bool allFolders = false);
    void loadFolderBar();          // перестроить полосу папок
    void filterHistory(const QString &query);
    void filterPins(const QString &query);
    bool isInteractiveArea(const QPoint &localPos) const;

    // ── Ленивая загрузка картинок ────────────────────────────────────────────
    void loadNextImage(int gen);
    int  m_loadGeneration = 0;
    QList<QPair<QPointer<QLabel>, QString>> m_pendingImages;

    // ── Вставка ──────────────────────────────────────────────────────────────
    void pasteText(const QString &text);
    void pasteImage(const QString &filepath);
    void pasteFile(const QString &filepath);  // CF_HDROP — для видео и аудио

    // ── Профили ──────────────────────────────────────────────────────────────
    void showProfilesMenu();    // выпадающее меню быстрой вставки
    void showProfileManager();  // диалог создания / редактирования / удаления

    // ── Настройки ─────────────────────────────────────────────────────────────
    void showSettings();

    // ── Контекстные меню ─────────────────────────────────────────────────────
    void showTextContextMenu(int id, const QString &content,
                             QLabel *card, const QPoint &globalPos);
    void showImageContextMenu(int id, const QString &filepath,
                              QLabel *card, const QPoint &globalPos);
    void showPinContextMenu(int pinId, const QString &currentFolder,
                            QLabel *card, const QPoint &globalPos);

    // ── Действия ─────────────────────────────────────────────────────────────
    void pinText(const QString &content);
    void pinImage(const QString &filepath);
    void importVideo();   // диалог выбора видео → в закрепы
    void importAudio();   // диалог выбора аудио → в закрепы
    void deleteCard(int id, QLabel *card);
    void deletePinCard(int pinId, QLabel *card);
    void showImageViewer(const QString &filepath);
    void editPin(int id, const QString &content, QLabel *card);
    void showTagsDialog(int pinId, QWidget *card);  // диалог редактирования тегов

    // Диалог выбора папки (используется при закреплении и перемещении)
    // Возвращает имя папки или "" если выбрано "Без папки". cancelled = true если отмена
    QString askFolder(bool &cancelled);

    // ── Данные ───────────────────────────────────────────────────────────────
    Database    *m_db;
    bool         m_historyDirty = true;
    ImageViewer *m_imageViewer  = nullptr;

    // HWND окна, которое было активным ДО открытия SmartClip.
    // Используется для возврата фокуса перед вставкой (Ctrl+V идёт в нужное окно).
    quintptr     m_prevFocusHwnd = 0;  // хранится как quintptr, кастуем в HWND при использовании

    // ── Виджеты ──────────────────────────────────────────────────────────────
    QVBoxLayout *m_leftLayout;
    QVBoxLayout *m_rightLayout;
    QScrollArea *m_leftScroll;
    QScrollArea *m_rightScroll;
    QWidget     *m_topPanel;
    QWidget     *m_foldersBar   = nullptr;  // полоса папок (только в режиме Закрепов)
    QHBoxLayout *m_foldersLayout = nullptr; // лейаут внутри полосы папок
    QWidget     *m_bottomPanel;
    QWidget     *m_centerSpacer;
    QLineEdit   *m_searchEdit;  // реально RoundedLineEdit, объявлен в .cpp
    SmartButton *m_historyBtn  = nullptr;
    SmartButton *m_pinsBtn     = nullptr;
    SmartButton *m_editBtn     = nullptr;   // кнопка режима мультивыбора (только в Закрепах)
    SmartButton *m_profilesBtn = nullptr;
    SmartButton *m_sortBtn     = nullptr;
    SmartButton *m_clearBtn    = nullptr;
    SmartButton *m_settingsBtn = nullptr;

    static const int TOP_HEIGHT     = 60;
    static const int FOLDERS_HEIGHT = 44;
    static const int BOTTOM_HEIGHT  = 54;
    static const int LEFT_WIDTH     = 300;
    static const int RIGHT_WIDTH    = 300;
};

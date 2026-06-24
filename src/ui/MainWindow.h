// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║  MainWindow.h — Главное окно SmartClip                                     ║
// ║                                                                              ║
// ║  MainWindow — это весь видимый интерфейс SmartClip: две колонки            ║
// ║  (история и закрепы), строка поиска, кнопки управления.                    ║
// ║                                                                              ║
// ║  Структура интерфейса:                                                       ║
// ║  ┌─────────────────────────────────────────────────────────────────┐        ║
// ║  │                    TopPanel (кнопки + поиск)                    │        ║
// ║  ├─────────────────────────────────────────────────────────────────┤        ║
// ║  │         [полоса папок — только в режиме Закрепов]              │        ║
// ║  ├─────────────────┬──────────────┬──────────────────────────────-─┤        ║
// ║  │  LeftColumn     │   CenterGap  │   RightColumn                  │        ║
// ║  │  (история или   │  (прозрачно) │   (история или закрепы)        │        ║
// ║  │   закрепы)      │              │                                 │        ║
// ║  ├─────────────────┴──────────────┴─────────────────────────────────┤       ║
// ║  │                    BottomPanel (навигация)                       │       ║
// ║  └─────────────────────────────────────────────────────────────────┘        ║
// ╚══════════════════════════════════════════════════════════════════════════════╝

#pragma once

// Все необходимые Qt-заголовки:
#include <QWidget>       // Базовый класс для всех видимых элементов Qt
#include <QVBoxLayout>   // Вертикальное расположение элементов (сверху вниз)
#include <QHBoxLayout>   // Горизонтальное расположение (слева направо)
#include <QScrollArea>   // Прокручиваемая область (для длинных списков карточек)
#include <QLineEdit>     // Строка ввода (поле поиска)
#include <QPointer>      // «Умный» указатель Qt: автоматически обнуляется если объект удалён
#include <QList>         // Динамический список
#include <QPair>         // Пара значений (для отложенной загрузки картинок)
#include <QLabel>        // Виджет для текста и изображений (карточки истории)
#include <QPushButton>   // Кнопки
#include <QPixmap>       // Изображение в видеопамяти
#include <QSet>          // Множество уникальных значений (выбранные пины)
#include "SmartButton.h" // Наша кастомная кнопка с ручной отрисовкой

// Предварительные объявления — нам нужны только указатели:
class Database;      // База данных
class ImageViewer;   // Полноэкранный просмотрщик изображений

// ─── Класс MainWindow ─────────────────────────────────────────────────────────
// Главное окно SmartClip. Не имеет системной рамки — рисуется полностью сам.
class MainWindow : public QWidget
{
    Q_OBJECT  // Макрос Qt для сигналов/слотов

public:
    // Конструктор: создаёт весь интерфейс.
    // db — база данных для чтения истории и закрепов.
    explicit MainWindow(Database *db, QWidget *parent = nullptr);

    // Деструктор: освобождает ресурсы (blur-окна, системные хуки и т.д.)
    ~MainWindow();

// ─── Публичные слоты (методы вызываемые через сигналы) ───────────────────────
public slots:
    // Показать окно SmartClip и вывести на передний план.
    // Вызывается из HotkeyManager когда пользователь нажимает горячую клавишу.
    void showWindow();

    // Пометить историю как «устаревшую» — при следующем показе окна перезагрузить.
    // Вызывается ClipboardManager когда в истории появилась новая запись.
    void markHistoryDirty();

    // Вставить текст без открытия SmartClip (хоткей профиля — фоновая вставка).
    void pasteTextSilent(const QString &text);

    // Обработать уведомление о новой версии от UpdateChecker.
    // Показывает диалог с предложением обновиться (только для major/minor обновлений).
    void onUpdateAvailable(const QString &version, const QString &downloadUrl,
                           const QString &releaseNotes = {});

// ─── Сигналы ─────────────────────────────────────────────────────────────────
signals:
    // Запрос на вставку содержимого из буфера в активное приложение.
    void pasteRequested();

    // Пользователь изменил или создал профили → HotkeyManager должен перерегистрировать хоткеи.
    void profilesChanged();

    // Пользователь сменил главную горячую клавишу в настройках.
    void mainHotkeyChanged(const QString &hotkey);

    // Любые настройки сохранены (для оповещения других компонентов).
    void settingsChanged();

// ─── Переопределённые методы QWidget ─────────────────────────────────────────
protected:
    // Обработка нативных Windows-сообщений (WM_ACTIVATE, WM_NCHITTEST и т.д.)
    // Нужен для реализации «исчезания при потере фокуса» и кастомного хиттестинга.
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

    // Скрытие окна — прячем также blur-окна через dcompBlurHide().
    void hideEvent(QHideEvent *event) override;

    // Нажатие клавиши — Escape закрывает окно.
    void keyPressEvent(QKeyEvent *event) override;

    // Перерисовка — SmartClip рисует свой фон вручную (без стандартной рамки Qt).
    void paintEvent(QPaintEvent *event) override;

private:
    // ─── Режимы отображения ───────────────────────────────────────────────────
    // SmartClip может показывать либо историю буфера, либо закрепы.
    enum class ViewMode { History, Pins };
    ViewMode m_viewMode      = ViewMode::History;  // Текущий режим
    QString  m_currentFolder = "";                  // Текущая папка в режиме Pins ("" = все)

    // Переключить режим отображения (History ↔ Pins).
    void switchView(ViewMode mode);

    // ─── Режим мультивыбора закрепов ─────────────────────────────────────────
    // Пользователь может выбрать несколько закрепов и удалить/переместить разом.
    bool      m_editMode       = false;   // true = включён режим выбора
    QSet<int> m_selectedPinIds;           // ID выбранных закрепов

    void toggleEditMode();                // Включить/выключить режим
    void showPinBatchContextMenu(const QPoint &globalPos);  // Контекстное меню для выбранных

    // ─── Drag-to-reorder закрепов ─────────────────────────────────────────────
    // Пользователь может перетаскивать карточки закрепов мышью чтобы изменить порядок.
    class ClickableCard *m_dragCard   = nullptr;  // Карточка которую тащим
    QVBoxLayout         *m_dragLayout = nullptr;  // Колонка в которой перемещаем

    void onCardDragMoved(const QPoint &globalPos);   // Обновить положение во время перетаскивания
    void onCardDragFinished();                        // Сохранить новый порядок
    void savePinsColumnOrder(QVBoxLayout *layout);   // Записать новый порядок в БД

    // ─── Сортировка ───────────────────────────────────────────────────────────
    enum class SortOrder { DateDesc, DateAsc, NameAsc, NameDesc };
    SortOrder m_sortOrder = SortOrder::DateDesc;  // По умолчанию — новые сверху

    void showSortMenu();                    // Показать выпадающее меню сортировки
    void applySortOrder(SortOrder order);   // Применить выбранный порядок
    QString sortLabel() const;              // Текст кнопки сортировки ("▼ Дата" и т.д.)

    // ─── Очистка истории ──────────────────────────────────────────────────────
    void showClearMenu();                          // Меню выбора что очистить
    void clearHistory(const QString &type);        // "" = всё, "text", "image", ...
    void clearHistoryByApp();                      // Очистить записи от определённого приложения
    bool confirmTwice(const QString &title,        // Двойное подтверждение (защита от случайного нажатия)
                      const QString &msg1,
                      const QString &msg2);

    // ─── Загрузка и отображение данных ───────────────────────────────────────
    void setupLayout();                     // Создать все виджеты и лейауты (вызывается в конструкторе)
    void loadHistory();                     // Загрузить историю из БД и отрисовать карточки
    void loadPins(bool allFolders = false); // Загрузить закрепы (всей папки или всех сразу)
    void loadFolderBar();                   // Перестроить полосу папок (вкладки папок в режиме Pins)
    void filterHistory(const QString &query);  // Фильтровать историю по тексту поиска
    void filterPins(const QString &query);     // Фильтровать закрепы
    bool isInteractiveArea(const QPoint &localPos) const;  // Попадает ли точка в область с виджетами

    // ─── Ленивая загрузка картинок ───────────────────────────────────────────
    // Картинки загружаются не сразу все — по одной через очередь.
    // Это предотвращает «заморозку» интерфейса при открытии большой истории.
    void loadNextImage(int gen);           // Загрузить следующую картинку в очереди
    int  m_loadGeneration = 0;             // Счётчик поколений: при обновлении списка — инкремент,
                                            // старые загрузки видят устаревший gen и прерываются
    QList<QPair<QPointer<QLabel>, QString>> m_pendingImages;  // Очередь: (label, путь_к_файлу)

    // ─── Вставка в активное приложение ───────────────────────────────────────
    void pasteText(const QString &text);        // Поместить текст в буфер и нажать Ctrl+V
    void pasteImage(const QString &filepath);   // Поместить картинку в буфер и нажать Ctrl+V
    void pasteFile(const QString &filepath);    // Вставить файл через CF_HDROP (для видео/аудио)

    // ─── Профили быстрой вставки ─────────────────────────────────────────────
    void showProfilesMenu();    // Выпадающее меню со списком профилей
    void showProfileManager();  // Диалог создания/редактирования/удаления профилей

    // ─── Настройки ───────────────────────────────────────────────────────────
    void showSettings();  // Открыть SettingsDialog

    // ─── Контекстные меню (правая кнопка мыши на карточке) ───────────────────
    void showTextContextMenu(int id, const QString &content,
                             QLabel *card, const QPoint &globalPos);
    void showImageContextMenu(int id, const QString &filepath,
                              QLabel *card, const QPoint &globalPos);
    void showPinContextMenu(int pinId, const QString &currentFolder,
                            QLabel *card, const QPoint &globalPos);

    // ─── Действия над карточками ──────────────────────────────────────────────
    void pinText(const QString &content);       // Закрепить текст из истории
    void pinImage(const QString &filepath);     // Закрепить картинку из истории
    void importVideo();                         // Выбрать видеофайл и добавить в закрепы
    void importAudio();                         // Выбрать аудиофайл и добавить в закрепы
    void deleteCard(int id, QLabel *card);      // Удалить запись истории
    void deletePinCard(int pinId, QLabel *card);// Удалить закреп
    void showImageViewer(const QString &filepath);          // Открыть полноэкранный просмотрщик
    void editPin(int id, const QString &content, QLabel *card);  // Редактировать текст закрепа
    void showTagsDialog(int pinId, QWidget *card);          // Диалог редактирования тегов закрепа

    // Диалог выбора папки (при закреплении или перемещении элемента).
    // Возвращает имя выбранной папки, или "" если выбрано «Без папки».
    // cancelled = true если пользователь нажал Отмена.
    QString askFolder(bool &cancelled);

    // ─── Данные ───────────────────────────────────────────────────────────────
    Database    *m_db;                   // База данных SmartClip
    bool         m_historyDirty = true;  // true = история устарела, нужно перезагрузить при показе
    ImageViewer *m_imageViewer  = nullptr; // Полноэкранный просмотрщик (создаётся при первом использовании)

    // Ожидающее обновление (данные о новой версии от UpdateChecker).
    // Хранятся чтобы передать в SettingsDialog (жёлтая плашка) если диалог открылся.
    QString m_pendingUpdateVersion;   // "1.1.0" или пусто
    QString m_pendingUpdateUrl;       // Ссылка на установщик
    QString m_pendingReleaseNotes;    // Описание что нового

    // ─── Масштаб интерфейса ───────────────────────────────────────────────────
    // Читается из AppSettings ОДИН РАЗ при запуске. Для смены нужен перезапуск.
    int m_scale = 100;  // Текущий масштаб в процентах (75, 90, 100, 110, 125)

    // Масштабировать числовое значение (пиксели).
    // Пример: sc(300) при scale=125 → 375px
    int sc(int v) const { return v * m_scale / 100; }

    // Масштабировать значение и вернуть строку "Npx" для CSS/QSS.
    // Пример: spx(12) при scale=125 → "15px"
    QString spx(int v) const { return QString::number(sc(v)) + "px"; }

    // HWND окна которое было в фокусе ДО открытия SmartClip.
    // Перед вставкой Ctrl+V мы возвращаем фокус этому окну.
    // quintptr — беззнаковое целое размером с указатель (32 или 64 бит).
    quintptr m_prevFocusHwnd = 0;

    // ─── Виджеты интерфейса ───────────────────────────────────────────────────
    QVBoxLayout *m_leftLayout;           // Лейаут левой колонки (карточки)
    QVBoxLayout *m_rightLayout;          // Лейаут правой колонки
    QScrollArea *m_leftScroll;           // Прокрутка левой колонки
    QScrollArea *m_rightScroll;          // Прокрутка правой колонки
    QWidget     *m_topPanel;             // Верхняя панель (кнопки режима и поиск)
    QWidget     *m_foldersBar   = nullptr; // Полоса вкладок папок (только в режиме Pins)
    QHBoxLayout *m_foldersLayout = nullptr;// Лейаут внутри полосы папок
    QWidget     *m_bottomPanel;          // Нижняя панель (История/Закрепы, профили, настройки)
    QWidget     *m_centerSpacer;         // Центральная прозрачная область между колонками
    QLineEdit   *m_searchEdit;           // Строка поиска (реально RoundedLineEdit из .cpp)
    SmartButton *m_historyBtn  = nullptr; // Кнопка «История»
    SmartButton *m_pinsBtn     = nullptr; // Кнопка «Закрепы»
    SmartButton *m_editBtn     = nullptr; // Кнопка мультивыбора (только в режиме Закрепов)
    SmartButton *m_profilesBtn = nullptr; // Кнопка «Профили»
    SmartButton *m_sortBtn     = nullptr; // Кнопка сортировки
    SmartButton *m_clearBtn    = nullptr; // Кнопка очистки истории
    SmartButton *m_settingsBtn = nullptr; // Кнопка настроек (шестерёнка)

    // ─── Базовые размеры при масштабе 100% ───────────────────────────────────
    // constexpr — значения известны на этапе компиляции, не занимают место в памяти объекта.
    // static    — принадлежат классу (одни для всех объектов MainWindow).
    static constexpr int BASE_TOP   = 60;  // Высота верхней панели, px
    static constexpr int BASE_FOLD  = 44;  // Высота полосы папок, px
    static constexpr int BASE_BOT   = 54;  // Высота нижней панели, px
    static constexpr int BASE_LEFT  = 300; // Ширина левой колонки, px
    static constexpr int BASE_RIGHT = 300; // Ширина правой колонки, px
};

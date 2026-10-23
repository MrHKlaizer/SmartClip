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
#include <QPointer>
#include "Anim.h"         // Анимации 2.0 (Anim::Piece)      // «Умный» указатель Qt: автоматически обнуляется если объект удалён
#include <QList>         // Динамический список
#include <QPair>         // Пара значений (для отложенной загрузки картинок)
#include <QLabel>        // Виджет для текста и изображений (карточки истории)
#include <QPushButton>   // Кнопки
#include <QPixmap>       // Изображение в видеопамяти
#include <QSet>          // Множество уникальных значений (выбранные пины)
#include "core/Database.h" // HistoryItem
#include "SmartButton.h" // Наша кастомная кнопка с ручной отрисовкой

// Предварительные объявления — нам нужны только указатели:
class Database;      // База данных
namespace Files { struct Entry; }  // Файл в полоске внизу (FilesStrip.h)
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

    // Закрыть окно с анимацией ухода (обратная лесенка, в 2 раза быстрее).
    // Для вставки по клику на карточку — обычный hide(), без задержки.
    void hideAnimated();

    // Открыто ли окно для пользователя (видно и не уезжает прямо сейчас).
    // Во время анимации ухода isVisible() ещё true — для переключения по
    // горячей клавише нужно именно это.
    bool isOpen() const;

    // Пометить историю как «устаревшую» — при следующем показе окна перезагрузить.
    // Вызывается ClipboardManager когда в истории появилась новая запись.
    void markHistoryDirty();

    // Вставить текст без открытия SmartClip (хоткей профиля — фоновая вставка).
    void pasteTextSilent(const QString &text);

    // Обработать уведомление о новой версии от UpdateChecker.
    // Запоминает обновление; сам диалог открывается по клику в трее или при открытии окна.
    // sha256 — контрольная сумма файла из GitHub (может быть пустой).
    void onUpdateAvailable(const QString &version, const QString &downloadUrl,
                           const QString &releaseNotes = {}, const QString &sha256 = {});

    // Открыть диалог для найденного обновления (клик по уведомлению в трее).
    void showPendingUpdate();

    // ─── Окно без захвата фокуса ─────────────────────────────────────────────
    // SmartClip открывается, не отбирая фокус у программы, где работает
    // пользователь, — иначе поля, которые закрываются при потере фокуса
    // (переименование файла и т.п.), закрывались, и вставлять было некуда.
    // Фокус берём, только когда пользователь начинает печатать или кликает в поиск.
    // Эти методы вызывают глобальные хуки клавиатуры/мыши (MainWindow.cpp).
    void onHookKey(unsigned vk, unsigned scan, bool extended); // Клавиша, пока мы без фокуса
    void onGlobalClick();                                      // Клик где угодно на экране
    void onForegroundChanged(quintptr hwnd);                   // Сменилось активное окно

    // Показать диалог скачивания обновления БЕЗ проверки major/minor.
    // Вызывается кнопкой «Обновить» из плашки в настройках — пользователь
    // уже явно попросил обновление, фильтровать patch-версии нельзя.
    void showUpdateDialog(const QString &version, const QString &downloadUrl,
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
    // Следим за кнопками История/Закрепы, чтобы «пилюля» стояла на месте
    bool eventFilter(QObject *obj, QEvent *event) override;

    // Обработка нативных Windows-сообщений (WM_ACTIVATE, WM_NCHITTEST и т.д.)
    // Нужен для реализации «исчезания при потере фокуса» и кастомного хиттестинга.
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

    // Скрытие окна — снимаем хуки ввода и прячем просмотр картинок.
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
    QPointer<class ClickableCard> m_dragCard;     // Карточка которую тащим (QPointer — сам обнулится, если карточку удалят)
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
    void loadHistory(bool textsOnly = false); // Загрузить историю из БД (textsOnly — только тексты, для поиска)
    // Подгрузка истории порциями при прокрутке
    void appendTexts();                      // Следующие TEXT_PAGE текстов
    void appendMedia();                      // Следующие MEDIA_PAGE картинок/видео
    void maybeLoadMore(class QScrollArea *area); // Долистали почти до конца — подгрузить
    // insertAt — место в колонке (-1 — в конец)
    void addTextCard(const HistoryItem &entry, int insertAt = -1);
    void addImageCard(const HistoryItem &entry, int insertAt = -1);
    void addVideoCard(const HistoryItem &entry, int insertAt = -1);
    void addAudioCard(const HistoryItem &entry, int insertAt = -1);  // Аудио — в колонке текстов
    void refreshHistory();                   // После копирования: только новые карточки сверху
    void restoreHistoryPos();                // Вернуть Историю туда, где остановились
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
    void closeAllOnDeactivate();   // Клик мимо при открытом окне/меню — закрыть всё
    QList<Anim::Piece> buildOpenPieces(); // Куски окна для анимации сборки/ухода

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
    void pinFile(const QString &filepath);      // Закрепить файл из полоски внизу
    void showFileMenu(const Files::Entry &e, const QPoint &globalPos);  // Правый клик по файлу
    void startFileDragSqueeze();  // Понесли файл — SmartClip прижимается к краям экрана
    void endFileDragSqueeze();    // Отпустили — отпрыгивает на место
    void dropSqueeze();           // SmartClip прячется — убрать сцену сразу
    void placeFoldersBar();       // Полоса папок — над левой колонкой, до правой
    void placeImportButtons();    // Кнопки импорта видео/аудио — по бокам полоски файлов
    void prewarm();               // Подготовить окно заранее, пока скрыто (быстрое первое открытие)
    void refreshPinnedFiles(bool allFolders = false);  // Закреплённые файлы и вкладки -> полоска
    QStringList pinFileTabs() const;  // Вкладки полоски в текущей папке закрепов
    void addFilesTab();                                         // «+» в полоске файлов
    void showFilesTabMenu(const QString &key, const QPoint &globalPos);  // Правый клик по вкладке
    void moveFilesToTab(const QList<Files::Entry> &files, const QString &tab);
    void goToFolder(const QString &folder);  // Открыть папку закрепов (с анимацией)
    void importVideo();                         // Выбрать видеофайл и добавить в закрепы
    void importAudio();                         // Выбрать аудиофайл и добавить в закрепы
    void deleteCard(int id, QLabel *card);      // Удалить запись истории
    void deletePinCard(int pinId, QLabel *card);// Удалить закреп
    void showImageViewer(const QString &filepath, QWidget *source = nullptr);          // Открыть полноэкранный просмотрщик
    void editPin(int id, const QString &content, QLabel *card);  // Редактировать текст закрепа
    void showTagsDialog(int pinId, QWidget *card);          // Диалог редактирования тегов закрепа

    // Диалог выбора папки (при закреплении или перемещении элемента).
    // Возвращает имя выбранной папки, или "" если выбрано «Без папки».
    // cancelled = true если пользователь нажал Отмена.
    QString askFolder(bool &cancelled);

    // ─── Данные ───────────────────────────────────────────────────────────────
    Database    *m_db;                   // База данных SmartClip
    QPointer<Anim::SnapshotOverlay> m_overlay; // Текущая сцена анимации окна (появление/уход)
    class SmoothScroller *m_leftScroller  = nullptr; // Плавная прокрутка левой колонки
    class SmoothScroller *m_rightScroller = nullptr; // ...и правой
    class CardArc        *m_leftArc  = nullptr;      // Прокрутка «Дуга» левой колонки
    class CardArc        *m_rightArc = nullptr;      // ...и правой
    class DateRuler      *m_leftRuler  = nullptr;    // Шкала дат левой колонки
    class DateRuler      *m_rightRuler = nullptr;    // ...и правой
    bool         m_hidePending = false;  // Уход начался, окно спрячется через пару кадров
    int          m_hideSeq     = 0;      // Счётчик: отменяет устаревшее отложенное скрытие
    bool         m_historyDirty = true;  // true = история устарела, нужно перезагрузить при показе
    // Подгрузка истории порциями: сколько уже показано и всё ли закончилось
    static constexpr int TEXT_PAGE  = 40;   // Текстов за раз
    static constexpr int MEDIA_PAGE = 20;   // Картинок/видео за раз
    int          m_textOffset  = 0;
    int          m_mediaOffset = 0;
    bool         m_textsDone   = false;
    bool         m_mediaDone   = false;
    bool         m_searching   = false;     // Идёт поиск — тексты берём по m_searchIds
    // Где остановились в Истории перед переходом в Закрепы
    struct HistoryPos { int left = 0, right = 0, texts = 0, media = 0; bool valid = false; };
    HistoryPos   m_histPos;
    QList<int>   m_searchIds;               // id всех совпадений поиска (показываются порциями)
    QList<QPair<int, QString>> m_searchIndex; // Для поиска: id и начало текста (из базы, один раз)
    bool         m_searchIndexValid = false;  // Индекс актуален (история не менялась)
    bool         m_historyLoaded = false;   // История уже загружена — можно обновлять быстро
    int          m_topTextId  = 0;          // Самая свежая загруженная запись: тексты
    int          m_topMediaId = 0;          // ...и картинки/видео
    class QTimer *m_searchTimer = nullptr; // Задержка поиска по истории (не искать на каждую букву)
    ImageViewer *m_imageViewer  = nullptr; // Полноэкранный просмотрщик (создаётся при первом использовании)
    class FilesStrip  *m_filesStrip = nullptr;  // Свежие файлы внизу по центру
    QPointer<QWidget>  m_squeeze;               // Сцена «прижались к краям» (DragSqueeze.h)
    bool               m_fileDrag = false;      // Сейчас несут файл из полоски
    class SmartButton *m_importVideoBtn = nullptr; // 🎬 / 🎵 — импорт в текущую папку закрепов
    class SmartButton *m_importAudioBtn = nullptr;
    class VideoViewer *m_videoViewer = nullptr; // Большой просмотр видео (2.1)

    // Ожидающее обновление (данные о новой версии от UpdateChecker).
    // Хранятся чтобы передать в SettingsDialog (жёлтая плашка) если диалог открылся.
    QString m_pendingUpdateVersion;   // "1.1.0" или пусто
    QString m_pendingUpdateUrl;       // Ссылка на установщик
    QString m_pendingReleaseNotes;    // Описание что нового
    QString m_pendingUpdateSha256;    // Контрольная сумма установщика (из GitHub API)
    bool    m_updatePromptShown = false; // Диалог обновления уже предлагали в этой сессии

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

    void activateForInput();   // Забрать фокус (пользователь начал печатать / кликнул в поиск)
    void installInputHooks();  // Хуки клавиатуры/мыши/смены окна — пока окно на экране
    void removeInputHooks();
    void finishPaste();        // Спрятаться и вставить (Ctrl+V) в окно, где работал пользователь

    // Перетаскивание закрепов (карточка едет за мышью, соседи расступаются)
    void beginCardDrag(const QPoint &globalPos);
    void stopCardDrag();   // Прервать перетаскивание (список перестраивают)
    class CardArc *arcFor(QVBoxLayout *layout) const;
    class QScrollArea *scrollFor(QVBoxLayout *layout) const;
    QPointer<QWidget> m_dragPlaceholder;        // Пустое место, куда встанет карточка
    QPoint            m_dragGrab;               // За какую точку карточку взяли
    class QTimer     *m_dragScrollTimer = nullptr; // Автопрокрутка у края колонки

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
    QWidget     *m_viewPill    = nullptr; // «Пилюля» активной вкладки История/Закрепы
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
    static constexpr int ARC_AMP    = 26;  // Запас колонки к центру под выдвинутую мышью карточку, px
};

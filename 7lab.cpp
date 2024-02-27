#include "framework.h"
#include "7lab.h"
#include <iostream>
#include <tchar.h>
#include <windowsx.h>
#include <stdio.h>
#include <string>

#define STACK_SIZE (64*1024)

using namespace std;

struct ConfigSettings {
	int windowWidth = 320;
	int windowHeight = 240;
	int N = 3;
	int BGcolorR = 0;
	int BGcolorG = 0;
	int BGcolorB = 255;
	int LineColorR = 255;
	int LineColorG = 0;
	int LineColorB = 0;
};
static ConfigSettings settings;

const wchar_t* APP_NAME = L"ЗаКрестики";
auto APP_NAME2 = L"ЗаНолики";

HANDLE gameoverMutex;
HANDLE ticThread;
HANDLE tacThread;
HANDLE animThread;
int priority;

HANDLE hMapFileProcess;
int* pDataProcess;

HWND hWndMain;
WNDCLASS softwareMainClass;

static int** cells;
static int N;
static int numberOfMoves = 0;
static bool BGanimState = true;

HANDLE hMapFile;
int* pData;

UINT WM_CELL_CHANGED;
UINT WM_FINISH_ALL_PROCESS;

HBRUSH hBrush;

void ReadConfigSettingsVar(ConfigSettings& settings);
void WriteConfigSettingsVar(const ConfigSettings& settings);
LRESULT CALLBACK SoftwareMainProcedure(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
WNDCLASS NewWindowClass(HBRUSH BGColor, HCURSOR Cursor, HINSTANCE hInst, HICON Icon, LPCWSTR Name, WNDPROC Procedure);
int WINAPI createWindow(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR args, int nCmdShow, const wchar_t* appName, int posX, int posY);
DWORD WINAPI changeBGanim(LPVOID);
void isWin(int** array, int n, int countOfMoves, HANDLE gameoverMutex);
void getColor(int r, int g, int b, int delta);
bool isDiagonal(int** array, int symb, int n);
bool isLine(int** array, int symb, int n);

void firstWindow() {
	HINSTANCE hInst = GetModuleHandle(NULL);
	createWindow(hInst, NULL, 0, TRUE, APP_NAME, 100, 100);
}

void secondWindow() {
	HINSTANCE hInst2 = GetModuleHandle(NULL);
	createWindow(hInst2, NULL, 0, TRUE, APP_NAME2, 100 + settings.windowWidth, 100);
}

#pragma comment(linker, "/entry:mainCRTStartup")
int main() {
    // Регистрация пользовательский сообщений
    WM_CELL_CHANGED = RegisterWindowMessage(L"WM_CELL_CHANGED");
    WM_FINISH_ALL_PROCESS = RegisterWindowMessage(L"WM_FINISH_ALL_APP");
    // Создание File Mapping объекта для обмена данными между процессами
    hMapFileProcess = CreateFileMapping(
        INVALID_HANDLE_VALUE,   // Идентификатор файла
        NULL,                   // Защита
        PAGE_READWRITE,         // Доступ
        0,                      // Размер файла (0 - динамический)
        sizeof(int),    // Размер fileMapping
        L"ProcessMappingObject");    // Имя fileMapping

    if (hMapFileProcess == NULL) {
        cout << "Не удалось создать File Mapping объект" << endl;
        return 1;
    }
    // Получерие указаателя на область памяти для записи данных
    pDataProcess = (int*)MapViewOfFile(hMapFileProcess, FILE_MAP_WRITE, 0, 0, sizeof(int));
    if (pDataProcess == NULL) {
        cout << "Не удалось получить указатель на представленную область памяти" << endl;
        CloseHandle(hMapFileProcess);
        return 1;
    }

    pDataProcess[0]++; // Увеличение значения, хранящегося в области памяти, на 1
    
    ReadConfigSettingsVar(settings); // Чтение настроек из конф. файла

    if (pDataProcess[0] == 1) { 
        if (ticThread == NULL) {
            // Созданиеи стркутур 
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            // Обнуление структур
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));
            char arg[] = "C:\\Users\\johns\\source\\repos\\7lab\\x64\\Debug\\7lab.exe";
            wchar_t text[500];
            mbstowcs(text, arg, strlen(arg) + 1); // Строка arg преобразуется из char в wchar_t
            LPWSTR command = text;
            // Запускааем новый процесс, используя LPWSTR command, если он создан успешно, то дублируются дескрипторы процесса и потока + создаётся первое окно
            if (CreateProcess(NULL, command, NULL, NULL, 0, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { 
                DuplicateHandle(pi.hProcess, pi.hThread, pi.hProcess, &ticThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
                firstWindow();
            }
        }
    }
    if (pDataProcess[0] == 2) {
        if (tacThread == NULL) {
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));
            char arg[] = "C:\\Users\\johns\\source\\repos\\7lab\\x64\\Debug\\7lab.exe";
            wchar_t text[500];
            mbstowcs(text, arg, strlen(arg) + 1);
            LPWSTR command = text;
            if (CreateProcess(NULL, command, NULL, NULL, 0, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                DuplicateHandle(pi.hProcess, pi.hThread, pi.hProcess,
                    &tacThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
                secondWindow();
            }
        }
    }
    if (pDataProcess[0] > 3) {
        ::MessageBoxW(nullptr, L"Игра уже идёт", L"Ошибка", MB_OK);
    }
}

int WINAPI createWindow(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR args, int nCmdShow, const wchar_t* appName, int posX, int posY) {
    gameoverMutex = CreateMutex(NULL, NULL, L"Global\MyMutex"); // Создание мьютекс 
    // Если создание мьютекса не удалось, то происходит попыткаа открыть уже существующий мьютекс с тем же именем
    if (gameoverMutex == NULL) { 
        gameoverMutex = OpenMutex(
            MUTEX_ALL_ACCESS,
            FALSE,
            L"Global\MyMutex");
    }

    N = settings.N;
    // Создание File Mapping объекта 
    hMapFile = CreateFileMapping(
        INVALID_HANDLE_VALUE,   // Идентификатор файла
        NULL,                   // Защита
        PAGE_READWRITE,         // Доступ
        0,                      // Размер файла (0 - динамический)
        sizeof(int) * N * N,    // Размер fileMapping
        L"MyMappingObject");    // Имя fileMapping
    if (hMapFile == NULL) {
        cout << "Не удалось создать File Mapping объект" << std::endl;
        return 1;
    }
    // Получерие указаателя на область памяти для записи данных 
    pData = (int*)MapViewOfFile(hMapFile, FILE_MAP_WRITE, 0, 0, sizeof(cells) * N * N);
    if (pData == NULL) {
        cout << "Не удалось получить указатель на представленную область памяти" << endl;
        CloseHandle(hMapFile);
        return 1;
    }
    //Инициализация поля, если приложение уже запущено - инициализируем текущий прогресс
    cells = new int* [N];
    for (int i = 0; i < N; i++) {
        cells[i] = new int[N];
        memset(cells[i], 0, sizeof(int) * N);
    }
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (pData[i * N + j] != 0) {
                cells[i][j] = pData[i * N + j];
                //если есть число, отличное от нуля -> был произведен ход.
                numberOfMoves++;
            }
        }
    }
    // Создание класса окна с заданными параметрами
    softwareMainClass = NewWindowClass((HBRUSH)CreateSolidBrush(RGB(settings.BGcolorR, settings.BGcolorG, settings.BGcolorB)), LoadCursor(NULL, IDC_ARROW), hInst, LoadIcon(hInst, IDI_QUESTION), appName, SoftwareMainProcedure);
    // Регистрация класса окна с заданными парметрами
    if (!RegisterClassW(&softwareMainClass)) { return -1; }
    // Создание главного окна с заданными параметрами + его отображение
    hWndMain = CreateWindow(appName, appName, WS_OVERLAPPEDWINDOW | WS_VISIBLE, posX, posY, settings.windowWidth, settings.windowHeight, NULL, NULL, NULL, NULL);
    ShowWindow(hWndMain, SW_SHOW);
    UpdateWindow(hWndMain);

    animThread = CreateThread(NULL, STACK_SIZE, changeBGanim, NULL, 0, NULL); // Создание потока для анимации фона

    MSG softwareMainMessage = { 0 };
    // Цикл обработки сообщений
    while (GetMessage(&softwareMainMessage, NULL, NULL, NULL)) {
        TranslateMessage(&softwareMainMessage);
        DispatchMessage(&softwareMainMessage);
    }
    // Освобождение ресурсов
    UnmapViewOfFile(pData);
    CloseHandle(hMapFile);

    UnmapViewOfFile(pDataProcess);
    CloseHandle(hMapFileProcess);

    DeleteObject(ticThread);
    DeleteObject(tacThread);
    DeleteObject(animThread);
    DeleteObject(gameoverMutex);

    DestroyWindow(hWndMain);
    return 0;
}

WNDCLASS NewWindowClass(
    HBRUSH backgrondColor,
    HCURSOR cursor,
    HINSTANCE hInst,
    HICON icon,
    LPCWSTR name,
    WNDPROC procedure
) {
    WNDCLASS newWindowClass = { 0 };
    newWindowClass.hCursor = cursor;
    newWindowClass.hInstance = hInst;
    newWindowClass.hIcon = icon;
    newWindowClass.lpszClassName = name;
    newWindowClass.hbrBackground = backgrondColor;
    newWindowClass.lpfnWndProc = procedure;
    return newWindowClass;
}

LRESULT CALLBACK SoftwareMainProcedure(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    PAINTSTRUCT ps;
    static int LineColorR = settings.LineColorR;
    static int LineColorG = settings.LineColorG;
    static int LineColorB = settings.LineColorB;
    static int deltaColor = 5;
    static int BGcolorR = settings.BGcolorR;
    static int BGcolorG = settings.BGcolorG;
    static int BGcolorB = settings.BGcolorB;

    RECT rect;
    GetWindowRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    static int cellSize = min(width / N, height / N);

    if (msg == WM_CELL_CHANGED) {
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                cells[i][j] = pData[i * N + j];
            }
        }
        InvalidateRect(hWnd, NULL, TRUE);
        numberOfMoves++;
        isWin(cells, N, numberOfMoves, gameoverMutex);
        return 0;
    }

    if (msg == WM_FINISH_ALL_PROCESS) {
        //ExitProcess(0);
        PostQuitMessage(0);
    }

    switch (msg) {
    case WM_CREATE: {
        break;
    }
    case WM_PAINT: {
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        HBRUSH hBrush = CreateSolidBrush(RGB(LineColorR, LineColorG, LineColorB));

        int cellsWidth = 0;
        int cellsHeight = 0;

        cellsWidth = clientRect.right / N;
        cellsHeight = clientRect.bottom / N;

        //Отрисовка сетки, крестиков и ноликов
        for (int x = 0; x < clientRect.right; x += cellsWidth) {
            for (int y = 0; y < clientRect.bottom; y += cellsHeight) {
                int row = y / cellsHeight;
                int col = x / cellsWidth;
                //при n = 3 вылазит за границы массива
                if (row == N) {
                    row = N - 1;
                }
                if (col == N) {
                    col = N - 1;
                }
                RECT cellRect = { x, y, x + cellsWidth, y + cellsHeight };
                FrameRect(hdc, &cellRect, hBrush);
                //Крестики
                if (cells[row][col] == 1) {
                    MoveToEx(hdc, x, y, NULL);
                    LineTo(hdc, x + cellsWidth, y + cellsHeight);
                    MoveToEx(hdc, x + cellsWidth, y, NULL);
                    LineTo(hdc, x, y + cellsHeight);
                }
                //Нолики
                else if (cells[row][col] == 2) {
                    int right = x + cellsWidth;
                    int bottom = y + cellsHeight;
                    Ellipse(hdc, x, y, right, bottom);
                }
            }
        }
        EndPaint(hWnd, &ps);
        settings.LineColorR = LineColorR;
        settings.LineColorG = LineColorG;
        settings.LineColorB = LineColorB;
        WriteConfigSettingsVar(settings);
        DeleteObject(hBrush);
        break;
    }
    case WM_SIZE: {
        InvalidateRect(hWnd, NULL, TRUE);
        break;
    }
    //Поставить крестик или нолик
    case WM_LBUTTONDOWN: {
        if (numberOfMoves % 2 == 0 && softwareMainClass.lpszClassName == APP_NAME) {
            int xPos = LOWORD(lp);
            int yPos = HIWORD(lp);
            int cellsWidth = 0;
            int cellsHeight = 0;
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            cellsWidth = clientRect.right / N;
            cellsHeight = clientRect.bottom / N;
            int row = yPos / cellsHeight;
            int col = xPos / cellsWidth;
            if (cells[row][col] == 0) {
                pData[row * N + col] = 1;
                SendMessage(HWND_BROADCAST, WM_CELL_CHANGED, 0, 0);
            }
            return 0;
        }
        if (numberOfMoves % 2 == 1 && softwareMainClass.lpszClassName == APP_NAME) {
            MessageBox(NULL, _T("Cейчас ход ноликов!"), _T("TickTackToe"),
                MB_OK | MB_SETFOREGROUND);
            return 0;
        }

        if (numberOfMoves % 2 == 1 && softwareMainClass.lpszClassName == APP_NAME2) {
            int xPos = LOWORD(lp);
            int yPos = HIWORD(lp);
            int cellsWidth = 0;
            int cellsHeight = 0;
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            cellsWidth = clientRect.right / N;
            cellsHeight = clientRect.bottom / N;
            int row = yPos / cellsHeight;
            int col = xPos / cellsWidth;
            if (cells[row][col] == 0) {
                pData[row * N + col] = 2;
                SendMessage(HWND_BROADCAST, WM_CELL_CHANGED, 0, 0);
            }
            return 0;
        }
        if (numberOfMoves % 2 == 0 && softwareMainClass.lpszClassName == APP_NAME2) {
            MessageBox(NULL, _T("Cейчас ход крестиков!"), _T("TickTackToe"),
                MB_OK | MB_SETFOREGROUND);
            return 0;
        }

        break;
    }
    case WM_KEYDOWN: {
        if (wp == 0x31) { //1 (самый низкий приоритет)
            SetThreadPriority(animThread, THREAD_PRIORITY_IDLE);
            wstring c = L"Приоритет стал равен:  " + to_wstring(GetThreadPriority(animThread));
            MessageBox(NULL, c.c_str(), _T("TickTackToe"),
                MB_OK | MB_SETFOREGROUND);
        }
        else if (wp == 0x32) { //2 (самый высокий приоритет)
            SetThreadPriority(animThread, THREAD_PRIORITY_TIME_CRITICAL);
            wstring c = L"Приоритет стал равен:  " + to_wstring(GetThreadPriority(animThread));
            MessageBox(NULL, c.c_str(), _T("TickTackToe"),
                MB_OK | MB_SETFOREGROUND);
        }
        else if (wp == 0x33) { //3 (нормальный приоритет)
            SetThreadPriority(animThread, THREAD_PRIORITY_NORMAL);
            wstring c = L"Приоритет стал равен: " + to_wstring(GetThreadPriority(animThread));
            MessageBox(NULL, c.c_str(), _T("TickTackToe"),
                MB_OK | MB_SETFOREGROUND);
        }
        else if (wp == VK_SPACE) { // для остановки/возобновления градиента
            BGanimState = !BGanimState;
            if (!BGanimState)
                SuspendThread(animThread);
            else
                ResumeThread(animThread);
        }
        else if (wp == VK_ESCAPE) {
            DestroyWindow(hWnd);
        }

        else if ((GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'Q' || wp == 'q')) {
            DestroyWindow(hWnd);
        }
        else if ((GetKeyState(VK_SHIFT) & 0x8000) && (wp == 'C' || wp == 'c')) {
            HANDLE hProcess = NULL;
            HANDLE hThread = NULL;
            LPCWSTR notepadPath = L"C:\\Windows\\notepad.exe";
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            DWORD dwProcessId = 0;
            DWORD dwThreadId = 0;
            ZeroMemory(&si, sizeof(si));
            ZeroMemory(&pi, sizeof(pi));
            BOOL bCreateProcess = NULL;
            bCreateProcess = CreateProcess(notepadPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (LineColorR == 255) {
            LineColorR = 0;
        }
        else {
            LineColorR += deltaColor;
        }

        if (LineColorG == 255) {
            LineColorG = 0;
        }
        else {
            LineColorG += deltaColor;
        }

        if (LineColorB >= 255) {
            LineColorB = 0;
        }
        else {
            LineColorB += deltaColor;
        }

        COLORREF newColor = RGB(LineColorR, LineColorG, LineColorB);
        HDC hdc = GetDC(hWnd); // Получение контекста устройства
        SetDCPenColor(hdc, newColor);
        ReleaseDC(hWnd, hdc); // Освобождение контекста устройства
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        break;
    }
    case WM_DESTROY: {
        SendMessage(HWND_BROADCAST, WM_FINISH_ALL_PROCESS, 0, 0);
        settings.windowWidth = width;
        settings.windowHeight = height;
        settings.N = N;
        settings.BGcolorR = BGcolorR;
        settings.BGcolorG = BGcolorG;
        settings.BGcolorB = BGcolorB;
        settings.LineColorR = LineColorR;
        settings.LineColorG = LineColorG;
        settings.LineColorB = LineColorB;
        WriteConfigSettingsVar(settings);
        DeleteObject(hBrush);
        delete[] cells;
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProc(hWnd, msg, wp, lp);
    }
    DeleteObject(hWndMain);
    return 0;
}

void ReadConfigSettingsVar(ConfigSettings& settings) {
    FILE* file;
    fopen_s(&file, "config.txt", "rb");
    if (file != NULL) {
        fread(&settings, sizeof(ConfigSettings), 1, file);
        fclose(file);
    }
}

void WriteConfigSettingsVar(const ConfigSettings& settings) {
    FILE* file;
    fopen_s(&file, "config.txt", "wb");
    if (file != NULL) {
        fwrite(&settings, sizeof(ConfigSettings), 1, file);
        fclose(file);
    }
}

DWORD WINAPI changeBGanim(LPVOID) {
    while (true) {
        if (BGanimState) {
            getColor(settings.BGcolorR, settings.BGcolorG, settings.BGcolorB, 5);
            SetClassLongPtr(hWndMain, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(RGB(settings.BGcolorR, settings.BGcolorG, settings.BGcolorB)));
        }
        InvalidateRect(hWndMain, NULL, TRUE);

        Sleep(50);
    }
    return 0;
}

void getColor(int r, int g, int b, int delta) {
    if (delta > 0) {
        if (r >= 255 && g < 255 && b <= 0) g += delta;
        else if (r > 0 && g >= 255 && b <= 0) r -= delta;
        else if (r <= 0 && g >= 255 && b < 255) b += delta;
        else if (r <= 0 && g > 0 && b >= 255) g -= delta;
        else if (r < 255 && g <= 0 && b >= 255) r += delta;
        else if (r >= 255 && g <= 0 && b > 0) b -= delta;
    }
    else {
        if (r >= 255 && g > 0 && b <= 0) g += delta;
        else if (r < 255 && g >= 255 && b <= 0) r -= delta;
        else if (r <= 0 && g >= 255 && b > 0) b += delta;
        else if (r <= 0 && g < 255 && b >= 255) g -= delta;
        else if (r > 0 && g <= 0 && b >= 255) r += delta;
        else if (r >= 255 && g <= 0 && b < 255) b -= delta;
    }
    if (r < 0) r = 255;
    else if (r > 255)  r = 0;
    if (b < 0) r = 255;
    else if (r > 255) r = 0;
    if (g < 0) r = 255;
    else if (r > 255) r = 0;

    settings.BGcolorR = r;
    settings.BGcolorG = g;
    settings.BGcolorB = b;
}

void isWin(int** array, int n, int countOfMoves, HANDLE gameoverMutex) {
    if (isDiagonal(array, 1, n) || isLine(array, 1, n)) {
        if (WaitForSingleObject(gameoverMutex, 0) == WAIT_TIMEOUT) {
            PostQuitMessage(0);
            return;
        }
        MessageBox(NULL, _T("Побед крестиков!"), _T("TickTackToe"), MB_OK | MB_SETFOREGROUND);

        PostQuitMessage(0);
    }

    if (isDiagonal(array, 2, n) || isLine(array, 2, n)) {
        if (WaitForSingleObject(gameoverMutex, 0) == WAIT_TIMEOUT) {
            PostQuitMessage(0);
            return;
        }
        MessageBox(NULL, _T("Победа ноликов!"), _T("TickTackToe"), MB_OK | MB_SETFOREGROUND);

        PostQuitMessage(0);
    }

    if (countOfMoves >= (n * n)) {
        if (WaitForSingleObject(gameoverMutex, 0) == WAIT_TIMEOUT) {
            PostQuitMessage(0);
            return;
        }
        MessageBox(NULL, _T("Ничья!"), _T("TickTackToe"), MB_OK | MB_SETFOREGROUND);

        PostQuitMessage(0);
    }
}

bool isDiagonal(int** array, int symb, int n) {
    bool toright = true;
    bool toleft = true;
    for (int i = 0; i < n; i++) {
        toright &= (array[i][i] == symb);
        toleft &= (array[n - i - 1][i] == symb);
    }
    if (toright || toleft) return true;

    return false;
}

bool isLine(int** array, int symb, int n) {
    bool cols = true;
    bool rows = true;
    for (int column = 0; column < n; column++) {
        cols = true;
        rows = true;
        for (int row = 0; row < n; row++) {
            cols &= (array[column][row] == symb);
            rows &= (array[row][column] == symb);
        }

        if (cols || rows) return true;
    }
    return false;
}

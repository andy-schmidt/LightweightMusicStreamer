#include "pch.h"
#include "resource.h"

using namespace winrt;
using namespace Windows::Foundation;

std::wstring MakeExceptionMessage()
{
    try
    {
        throw;
    }
    catch (const std::exception& e)
    {
        std::string message{ std::format("{}: {}", typeid(e).name(), e.what()) };

        int size{ MultiByteToWideChar(CP_ACP, 0, message.data(), static_cast<int32_t>(message.size()), nullptr, 0) };
        if (size == 0)
        {
            return{};
        }

        std::wstring wMessage(size, L'0');
        size = MultiByteToWideChar(CP_ACP, 0, message.data(), static_cast<int32_t>(message.size()), wMessage.data(), size);
        if (size == 0)
        {
            return std::format(L"Could not process std::exception. MultiByteToWideChar failed with {}", GetLastError());
        }
        return wMessage;
    }
    catch (const hresult_error& e)
    {
        return std::format(L"hresult_error {:#08x}: {}", static_cast<uint32_t>(e.code().value), static_cast<std::wstring_view>(e.message()));
    }
    catch (...)
    {
        return L"Unknown exception";
    }
}

void ExceptionMessageBox(HWND hDlg, const wchar_t* title)
{
    const auto errorMessage{ MakeExceptionMessage() };
    MessageBox(hDlg, errorMessage.c_str(), title, MB_ICONERROR | MB_ICONERROR);
}

struct StreamingPlayerDialog
{
    static constexpr auto g_deepinradio{ L"http://s3.viastreaming.net:8525" };
    static constexpr auto g_KCSM{ L"http://ice5.securenetsystems.net/KCSM" };

    enum class State
    {
        Playing,
        Stopped
    };
    State m_state{ State::Stopped };
    IAsyncInfo m_async{ nullptr };
    Windows::Media::Playback::MediaPlayer m_mediaPlayer{ nullptr };

    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    INT_PTR Run(HINSTANCE hInstance);
    void OpenAndPlayStreamingSource(HWND hDlg);
    void OnActionClick(HWND hDlg);
    ~StreamingPlayerDialog();
};

StreamingPlayerDialog::~StreamingPlayerDialog()
{
    if (m_async && m_async.Status() == AsyncStatus::Started)
    {
        m_async.Cancel();
    }
}

void StreamingPlayerDialog::OpenAndPlayStreamingSource(HWND hDlg)
{
    Uri uri{ g_deepinradio };
    auto streamingSource{ Windows::Media::Core::MediaSource::CreateFromUri(std::move(uri)) };
    m_mediaPlayer = Windows::Media::Playback::MediaPlayer{};

    auto async = streamingSource.OpenAsync();
    async.Completed([hDlg, player = m_mediaPlayer, source = std::move(streamingSource)](const IAsyncAction& async, const AsyncStatus status)
    {
        // We have to be careful about we do in this completion delegate. It usually runs on a background thread which
        // can lead to race conditions with the UI. Windows::Media::Playback::MediaPlayer and Windows::Media::Core::MediaSource
        // are both agile so they should implement the correct locks for multithreaded access.
        try
        {
            switch (status)
            {
            case AsyncStatus::Completed:
                player.Source(source);
                player.Play();
                break;
            case AsyncStatus::Error:
                throw_hresult(async.ErrorCode());
                break;
            }
        }
        catch (...)
        {
            ExceptionMessageBox(hDlg, L"Error opening music source");
        }
    });

    m_async = async;
}

void StreamingPlayerDialog::OnActionClick(HWND hDlg)
{
    switch (m_state)
    {
    case State::Stopped:
        OpenAndPlayStreamingSource(hDlg);
        SetDlgItemText(hDlg, IDACTION, L"Stop");
        m_state = State::Playing;
        break;

    case State::Playing:
        switch (m_async.Status())
        {
        case AsyncStatus::Completed:
            m_mediaPlayer.Pause();
            break;
        default:
            m_async.Cancel();
            break;
        }
        m_mediaPlayer = nullptr;
        m_async = nullptr;
        SetDlgItemText(hDlg, IDACTION, L"Play");
        m_state = State::Stopped;
    }
}

INT_PTR StreamingPlayerDialog::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto window{ reinterpret_cast<StreamingPlayerDialog*>(GetWindowLongPtr(hDlg, DWLP_USER)) };
    if (window == nullptr && message != WM_INITDIALOG)
    {
        return FALSE;
    }

    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowLongPtr(hDlg, DWLP_USER, lParam);
        return TRUE;
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
            break;
        case IDACTION:
            window->OnActionClick(hDlg);
            return TRUE;
            break;
        }
        break;
    }
    return FALSE;
}

template <class Dialog>
INT_PTR CALLBACK ExceptionGuardedDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    // This prevents crash dump reports from C++ exceptions. But this is not a commercial product, and we are not
    // setup to collect crash dumps from Microsoft. We get more useful visibility into what errors occur this way
    // by letting users send us the strange errors they see. This is somewhat comparable to a very aggressive 
    // Microsoft.UI.Xaml.Application.UnhandledException handler for the UI thread.

    try
    {
        return Dialog::DialogProc(hDlg, message, wParam, lParam);
    }
    catch (...)
    {
        ExceptionMessageBox(hDlg, L"Dialog callback error");
        return FALSE;
    }
}

INT_PTR StreamingPlayerDialog::Run(HINSTANCE hInstance)
{
    return DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_PLAYER), nullptr, ExceptionGuardedDlgProc<StreamingPlayerDialog>, reinterpret_cast<INT_PTR>(this));
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    init_apartment(apartment_type::single_threaded);

    StreamingPlayerDialog dialog;
    dialog.Run(hInstance);
}

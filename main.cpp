#include "pch.h"
#include "resource.h"

#include <array>
#include <exception>
#include <format>
#include <string>
#include <string_view>

using namespace std::string_view_literals;
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

struct StreamingSource
{
    const wchar_t* m_caption;
    const std::wstring_view m_uri;
};

constexpr std::array g_musicSources
{
    StreamingSource{ L"Deepinradio", L"http://s3.viastreaming.net:8525"sv },
    StreamingSource{ L"KCSM", L"http://ice5.securenetsystems.net/KCSM"sv },
    StreamingSource{ L"KZSC", L"https://kzscfms1-geckohost.radioca.st/kzschigh?type=.mp3"sv },
    StreamingSource{ L"KALX", L"http://stream.kalx.berkeley.edu:8000/kalx-320.aac"sv }
};

struct StreamingPlayerDialog
{
    static constexpr auto g_playGlyph{ L"\x23f5" };
    static constexpr auto g_stopGlyph{ L"\x23f9" };

    enum class State
    {
        Playing,
        Stopped
    };
    State m_state{ State::Stopped };

    IAsyncInfo m_async{ nullptr };
    Windows::Media::Playback::MediaPlayer m_mediaPlayer{ nullptr };
    HWND m_hDlg{ nullptr };

    static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    INT_PTR Run(HINSTANCE hInstance);
    void OnInitialize();
    void OnActionClick();
    void OnSourceChange();
    void OpenAndPlayStreamingSource();
    const StreamingSource& GetSource() const;
    ~StreamingPlayerDialog();
};

StreamingPlayerDialog::~StreamingPlayerDialog()
{
    if (m_async && m_async.Status() == AsyncStatus::Started)
    {
        m_async.Cancel();
    }
}

void StreamingPlayerDialog::OnInitialize()
{
    const auto comboHwnd{ GetDlgItem(m_hDlg, IDC_SOURCE) };
    for (const auto& source : g_musicSources)
    {
        const auto index{ SendMessage(comboHwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(source.m_caption)) };
        if (index == CB_ERR || index == CB_ERRSPACE)
        {
            throw_last_error();
        }
        check_bool(SendMessage(comboHwnd, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(&source)) != CB_ERR);
    }

    if constexpr (g_musicSources.size() > 0)
    {
        SendMessage(comboHwnd, CB_SETCURSEL, 0, 0);
    }
}

void StreamingPlayerDialog::OpenAndPlayStreamingSource()
{
    Uri uri{ GetSource().m_uri };
    auto streamingSource{ Windows::Media::Core::MediaSource::CreateFromUri(std::move(uri)) };
    m_mediaPlayer = Windows::Media::Playback::MediaPlayer{};
    m_mediaPlayer.CommandManager().IsEnabled(false);

    auto async = streamingSource.OpenAsync();
    async.Completed([hDlg = m_hDlg, player = m_mediaPlayer, source = std::move(streamingSource)](const IAsyncAction& async, const AsyncStatus status)
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

const StreamingSource& StreamingPlayerDialog::GetSource() const
{
    const auto comboHwnd{ GetDlgItem(m_hDlg, IDC_SOURCE) };

    const auto selectedIndex{ SendMessage(comboHwnd, CB_GETCURSEL, 0 , 0) };
    check_bool(selectedIndex != CB_ERR);

    const auto source{ SendMessage(comboHwnd, CB_GETITEMDATA, selectedIndex, 0) };
    check_bool(source != CB_ERR);

    return *reinterpret_cast<StreamingSource*>(source);
}

void StreamingPlayerDialog::OnActionClick()
{
    const auto comboHwnd{ GetDlgItem(m_hDlg, IDC_SOURCE) };

    switch (m_state)
    {
    case State::Stopped:
        OpenAndPlayStreamingSource();
        SetDlgItemText(m_hDlg, IDACTION, g_stopGlyph);
        EnableWindow(comboHwnd, FALSE);
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
        SetDlgItemText(m_hDlg, IDACTION, g_playGlyph);
        EnableWindow(comboHwnd, TRUE);
        m_state = State::Stopped;
        break;
    }
}

void StreamingPlayerDialog::OnSourceChange()
{
    const auto source{ GetSource() };
    SetWindowText(m_hDlg, source.m_caption);
}

INT_PTR StreamingPlayerDialog::DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto dialog{ reinterpret_cast<StreamingPlayerDialog*>(GetWindowLongPtr(hDlg, DWLP_USER)) };
    if (dialog == nullptr && message != WM_INITDIALOG)
    {
        return FALSE;
    }

    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowLongPtr(hDlg, DWLP_USER, lParam);
        dialog = reinterpret_cast<StreamingPlayerDialog*>(lParam);
        dialog->m_hDlg = hDlg;
        dialog->OnInitialize();
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
            dialog->OnActionClick();
            return TRUE;
            break;

        case IDC_SOURCE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                dialog->OnSourceChange();
                return TRUE;
            }
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

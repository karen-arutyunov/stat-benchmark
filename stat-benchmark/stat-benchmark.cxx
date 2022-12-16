#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX // No min and max macros.
#    define NOMINMAX
#    include <windows.h>
#    undef NOMINMAX
#  else
#    include <windows.h>
#  endif
#  undef WIN32_LEAN_AND_MEAN
#else
#  ifndef NOMINMAX
#    define NOMINMAX
#    include <windows.h>
#    undef NOMINMAX
#  else
#    include <windows.h>
#  endif
#endif

#include<io.h>    // _findclose()
#include <time.h> // gmtime

#include <ctime>        // tm, time_t
#include <chrono>
#include <memory>
#include <string>
#include <ostream>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <system_error>

struct failed {};

using namespace std;
using namespace std::chrono;

string
error_msg (DWORD code)
{
  struct msg_deleter {void operator() (char* p) const {LocalFree (p);}};

  char* msg;
  if (!FormatMessageA (
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_MAX_WIDTH_MASK,
        0,
        code,
        MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&msg,
        0,
        0))
    return "unknown error code " + to_string (code);

  unique_ptr<char, msg_deleter> m (msg);
  return msg;
}

static string
last_error_msg ()
{
  return error_msg (GetLastError ());
}

struct nullhandle_t
{
  constexpr explicit nullhandle_t (HANDLE) {}
  operator HANDLE () const {return INVALID_HANDLE_VALUE;}
};

static const nullhandle_t nullhandle (INVALID_HANDLE_VALUE);

class auto_handle
{
public:
  auto_handle (nullhandle_t = nullhandle) noexcept
    : handle_ (INVALID_HANDLE_VALUE) {}

  explicit
  auto_handle (HANDLE h) noexcept: handle_ (h) {}

  auto_handle (auto_handle&& h) noexcept: handle_ (h.release ()) {}
  auto_handle& operator= (auto_handle&&) noexcept;

  auto_handle (const auto_handle&) = delete;
  auto_handle& operator= (const auto_handle&) = delete;

  ~auto_handle () noexcept;

  HANDLE
  get () const noexcept {return handle_;}

  void
  reset (HANDLE h = INVALID_HANDLE_VALUE) noexcept;

  HANDLE
  release () noexcept
  {
    HANDLE r (handle_);
    handle_ = INVALID_HANDLE_VALUE;
    return r;
  }

  void
  close ();

private:
  HANDLE handle_;
};

inline void auto_handle::
reset (HANDLE h) noexcept
{
  // Don't check for an error as not much we can do here.
  //
  if (handle_ != INVALID_HANDLE_VALUE)
    CloseHandle (handle_);

  handle_ = h;
}

inline auto_handle& auto_handle::
operator= (auto_handle&& h) noexcept
{
  reset (h.release ());
  return *this;
}

inline auto_handle::
~auto_handle () noexcept
{
  reset ();
}

inline bool
operator== (const auto_handle& x, const auto_handle& y)
{
  return x.get () == y.get ();
}

inline bool
operator!= (const auto_handle& x, const auto_handle& y)
{
  return !(x == y);
}

inline bool
operator== (const auto_handle& x, nullhandle_t)
{
  return x.get () == INVALID_HANDLE_VALUE;
}

inline bool
operator!= (const auto_handle& x, nullhandle_t y)
{
  return !(x == y);
}

ostream&
to_stream (ostream& os, const system_clock::duration& d, bool ns)
{
  system_clock::time_point ts; // Epoch.
  ts += d;

  time_t t (system_clock::to_time_t (ts));

  const char* fmt (nullptr);
  const char* unt;
  if (t >= 365 * 24 * 60 * 60)
  {
    fmt = "%Y-%m-%d %H:%M:%S";
    unt = "years";
  }
  else if (t >= 31 * 24 * 60 * 60)
  {
    fmt = "%m-%d %H:%M:%S";
    unt = "months";
  }
  else if (t >= 24 * 60 * 60)
  {
    fmt = "%d %H:%M:%S";
    unt = "days";
  }
  else if (t >= 60 * 60)
  {
    fmt = "%H:%M:%S";
    unt = "hours";
  }
  else if (t >= 60)
  {
    fmt = "%M:%S";
    unt = "minutes";
  }
  else if (t >= 1)
  {
    fmt = "%S";
    unt = "seconds";
  }
  else
    unt = ns ? "nanoseconds" : "seconds";

  if (fmt != nullptr)
  {
    const tm* gt (gmtime (&t));

    if (gt == nullptr)
    {
      system_error e (errno, generic_category ());
      cerr << "error: gmtime() failed: " << e.what () << endl;
      throw failed ();
    }

    std::tm tm (*gt);

    if (t >= 24 * 60 * 60)
      tm.tm_mday -= 1; // Make day of the month to be a zero-based number.

    if (t >= 31 * 24 * 60 * 60)
      tm.tm_mon -= 1; // Make month of the year to be a zero-based number.

    if (t >= 365 * 24 * 60 * 60)
      // Make the year to be a 1970-based number. Negative values allowed
      // according to the POSIX specification.
      //
      tm.tm_year -= 1970;

    if (!(os << put_time (&tm, fmt)))
      return os;
  }

  using namespace chrono;

  if (ns)
  {
    system_clock::time_point sec (system_clock::from_time_t (t));
    nanoseconds nsec (duration_cast<nanoseconds> (ts - sec));

    if (nsec != nanoseconds::zero ())
    {
      if (fmt != nullptr)
      {
        ostream::fmtflags fl (os.flags ());
        char fc (os.fill ('0'));
        os << '.' << dec << right << setw (9) << nsec.count ();
        os.fill (fc);
        os.flags (fl);
      }
      else
        os << nsec.count ();
    }
    else if (fmt == nullptr)
      os << '0';
  }
  else if (fmt == nullptr)
    os << '0';

  os << ' ' << unt;
  return os;
}

inline ostream&
operator<< (ostream& os, const system_clock::duration& d)
{
  return to_stream (os, d, true);
}

int main (int argc, char* argv[])
{
  auto usage = [&argv] ()
  {
    cerr << "Usage:" << endl
         << "  " << argv[0] << " stat (-a|-e|-h) <file>" << endl
         << "  " << argv[0] << " iter [-a|-e|-h] <dir>" << endl;

    throw failed ();
  };

  try
  {
    bool attrs (false);
    bool attrs_ex (false);
    bool attrs_handle (false);

    int i (1);

    if (argc == 1)
      usage ();

    enum class cmd
    {
      stat,
      iter
    } c;

    string a (argv[i]);

    if (a == "stat")
      c = cmd::stat;
    else if (a == "iter")
      c = cmd::iter;
    else
      usage ();

    for (++i; i != argc; ++i)
    {
      string v (argv[i]);

      if (v == "-a")
        attrs = true;
      else if (v == "-e")
        attrs_ex = true;
      else if (v == "-h")
        attrs_handle = true;
      else
        break;
    }

    switch (c)
    {
    case cmd::stat:
      {
        if (i != argc - 1 || (attrs + attrs_ex + attrs_handle) != 1)
          usage ();

        string p (argv[i]);

        ifstream f (p);
        if (!f.is_open ())
        {
          cerr << "error: can't open " << p << endl;
          throw failed ();
        }

        f.exceptions (ifstream::badbit);

        size_t count (0);
        system_clock::time_point start_time (system_clock::now ());

        for (string p; getline (f, p); ++count)
        {
          if (attrs)
          {
            DWORD a (GetFileAttributesA (p.c_str ()));
            if (a == INVALID_FILE_ATTRIBUTES)
            {
              cerr << "error: GetFileAttributesA() failed for " << p << ": "
                   << last_error_msg () << endl;

              throw failed ();
            }
          }
          else if (attrs_ex)
          {
            WIN32_FILE_ATTRIBUTE_DATA a;
            if (!GetFileAttributesExA (p.c_str (), GetFileExInfoStandard, &a))
            {
              cerr << "error: GetFileAttributesExA() failed for " << p << ": "
                   << last_error_msg () << endl;

              throw failed ();
            }
          }
          else if (attrs_handle)
          {
            auto_handle h (
              CreateFile (p.c_str (),
                          0,
                          0,
                          nullptr,
                          OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS,// Required for a directory.
                          nullptr));

            if (h == nullhandle)
            {
              cerr << "error: CreateFile failed for " << p << ": "
                   << last_error_msg () << endl;

              throw failed ();
            }

            BY_HANDLE_FILE_INFORMATION r;
            if (!GetFileInformationByHandle (h.get (), &r))
            {
              cerr << "error: GetFileInformationByHandle() failed for " << p
                   << ": " << last_error_msg () << endl;

              throw failed ();
            }
          }
          else
            assert (false);
        }

        system_clock::time_point end_time (system_clock::now ());

        if (!f.eof ())
        {
          cerr << "error: can't read " << p << endl;
          throw failed ();
        }

        system_clock::duration d (end_time - start_time);
        system_clock::duration de (d / count);

        cerr << "entries: " << count << endl
             << "full time: " << d << endl
             << "time per entry: " << de << endl;

        break;
      }

    case cmd::iter:
      {
        // @@ FindFirstFileA

        if (i != argc - 1 || (attrs + attrs_ex + attrs_handle) > 1)
          usage ();

        string p (argv[i]);

        size_t count (0);

        auto iterate = [&count] (const string& d, const auto& iterate) -> void
        {
          struct auto_dir
          {
            explicit
            auto_dir (intptr_t& h): h_ (&h) {}

            auto_dir (const auto_dir&) = delete;
            auto_dir& operator= (const auto_dir&) = delete;

            ~auto_dir ()
            {
              if (h_ != nullptr && *h_ != -1)
                _findclose (*h_);
            }

            void release () {h_ = nullptr;}

          private:
            intptr_t* h_;
          };

          _finddata_t fi;

          intptr_t h = -1;
          auto_dir ad (h);

          for (;;)
          {
            bool r;

            if (h == -1)
            {
              h = _findfirst ((d + "\\*").c_str (), &fi);
              r = (h != -1);

//              cerr << "_findfirst: " << d << "\\*" << endl;
            }
            else
            {
              r = (_findnext (h, &fi) == 0);

//              cerr << "_findnext" << endl;
            }

            if (r)
            {
              string p (fi.name);

//              cerr << p << endl;

              if (p == "." || p == "..")
                continue;

              ++count;

              if ((fi.attrib & _A_SUBDIR) != 0)
                iterate (d + '\\' + p, iterate);
            }
            else if (errno == ENOENT)
            {
              // End of stream.
              //
              if (h != -1)
              {
                _findclose (h);
                h = -1;
                break;
              }
            }
            else
            {
              system_error e (errno, generic_category ());
              cerr << "error: _find*() failed: " << e.what () << endl;
              throw failed ();
            }
          }
        };

        system_clock::time_point start_time (system_clock::now ());

        iterate (p, iterate);

        system_clock::time_point end_time (system_clock::now ());

        system_clock::duration d (end_time - start_time);
        system_clock::duration de (d / count);

        cerr << "entries: " << count << endl
             << "full time: " << d << endl
             << "time per entry: " << de << endl;

        break;
      }
    }

    return 0;
  }
  catch (const failed&)
  {
    return 1;
  }
}

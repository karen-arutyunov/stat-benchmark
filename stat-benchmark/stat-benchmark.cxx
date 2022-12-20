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

using std::chrono::system_clock;
using std::chrono::nanoseconds;
using std::chrono::duration_cast;

using timestamp = system_clock::time_point;
using duration = system_clock::duration;

const timestamp::rep timestamp_unknown_rep     = -1;
const timestamp      timestamp_unknown         = timestamp (duration (-1));
const timestamp::rep timestamp_nonexistent_rep = 0;
const timestamp      timestamp_nonexistent     = timestamp (duration (0));
const timestamp::rep timestamp_unreal_rep      = 1;
const timestamp      timestamp_unreal          = timestamp (duration (1));

struct entry_time
{
  timestamp modification;
  timestamp access;
};

static inline bool
operator== (const entry_time& x, const entry_time& y)
{
  return x.modification == y.modification && x.access == y.access;
}

static string
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

static inline string
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

static inline bool
operator== (const auto_handle& x, const auto_handle& y)
{
  return x.get () == y.get ();
}

static inline bool
operator!= (const auto_handle& x, const auto_handle& y)
{
  return !(x == y);
}

static inline bool
operator== (const auto_handle& x, nullhandle_t)
{
  return x.get () == INVALID_HANDLE_VALUE;
}

static inline bool
operator!= (const auto_handle& x, nullhandle_t y)
{
  return !(x == y);
}

static ostream&
to_stream (ostream& os, const duration& d, bool ns)
{
  timestamp ts; // Epoch.
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

  if (ns)
  {
    timestamp sec (system_clock::from_time_t (t));
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

static inline ostream&
operator<< (ostream& os, const duration& d)
{
  return to_stream (os, d, true);
}

namespace details
{
  static tm*
  gmtime (const time_t* t, tm* r)
  {
#ifdef _WIN32
    const tm* gt (::gmtime (t));
    if (gt == nullptr)
      return nullptr;

    *r = *gt;
    return r;
#else
    return gmtime_r (t, r);
#endif
  }

  static tm*
  localtime (const time_t* t, tm* r)
  {
#ifdef _WIN32
    const tm* lt (::localtime (t));
    if (lt == nullptr)
      return nullptr;

    *r = *lt;
    return r;
#else
    return localtime_r (t, r);
#endif
  }
}


ostream&
to_stream (ostream& os,
           const timestamp& ts,
           const char* format,
           bool special,
           bool local)
{
  if (special)
  {
    if (ts == timestamp_unknown)
      return os << "<unknown>";

    if (ts == timestamp_nonexistent)
      return os << "<nonexistent>";

    if (ts == timestamp_unreal)
      return os << "<unreal>";
  }

  time_t t (system_clock::to_time_t (ts));

  std::tm tm;
  if ((local
       ? details::localtime (&t, &tm)
       : details::gmtime (&t, &tm)) == nullptr)
  {
    system_error e (errno, generic_category ());
    cerr << "error: localtime() or gmtime() failed: " << e.what () << endl;
    throw failed ();
  }

  timestamp sec (system_clock::from_time_t (t));
  nanoseconds ns (duration_cast<nanoseconds> (ts - sec));

  char fmt[256];
  size_t n (strlen (format));
  if (n + 1 > sizeof (fmt))
  {
    system_error e (EINVAL, generic_category ());
    cerr << "error: to_stream(timestamp) failed: " << e.what () << endl;
    throw failed ();
  }

  memcpy (fmt, format, n + 1);

  // Chunk the format string into fragments that we feed to put_time() and
  // those that we handle ourselves. Watch out for the escapes (%%).
  //
  size_t i (0), j (0); // put_time()'s range.

  // Print the time partially, using the not printed part of the format
  // string up to the current scanning position and setting the current
  // character to NULL. Return true if the operation succeeded.
  //
  auto print = [&i, &j, &fmt, &os, &tm] ()
    {
      if (i != j)
      {
        fmt[j] = '\0'; // Note: there is no harm if j == n.
        if (!(os << put_time (&tm, fmt + i)))
          return false;
      }

      return true;
    };

  for (; j != n; ++j)
  {
    if (fmt[j] == '%' && j + 1 != n)
    {
      char c (fmt[j + 1]);

      if (c == '[')
      {
        if (os.width () != 0)
        {
          cerr << "padding is not supported when printing nanoseconds" << endl;
          throw failed ();
        }

        // Our fragment.
        //
        if (!print ())
          return os;

        j += 2; // Character after '['.
        if (j == n)
        {
          system_error e (EINVAL, generic_category ());
          cerr << "error: to_stream(timestamp) failed: " << e.what () << endl;
          throw failed ();
        }

        char d ('\0');
        if (fmt[j] != 'N')
        {
          d = fmt[j];
          if (++j == n || fmt[j] != 'N')
          {
            system_error e (EINVAL, generic_category ());
            cerr << "error: to_stream(timestamp) failed: " << e.what () << endl;
            throw failed ();
          }
        }

        if (++j == n || fmt[j] != ']')
        {
          system_error e (EINVAL, generic_category ());
          cerr << "error: to_stream(timestamp) failed: " << e.what () << endl;
          throw failed ();
        }

        if (ns != nanoseconds::zero ())
        {
          if (d != '\0')
            os << d;

          ostream::fmtflags fl (os.flags ());
          char fc (os.fill ('0'));
          os << dec << right << setw (9) << ns.count ();
          os.fill (fc);
          os.flags (fl);
        }

        i = j + 1; // j is incremented in the for-loop header.
      }
      //
      // Note that MinGW (as of GCC 9.2) libstdc++'s implementations of
      // std::put_time() and std::strftime() don't recognize the %e
      // specifier, converting it into the empty string (bug #95624). Thus,
      // we handle this specifier ourselves for libstdc++ on Windows.
      //
#if defined(_WIN32) && defined(__GLIBCXX__)
      else if (c == 'e')
      {
        if (!print ())
          return os;

        ostream::fmtflags fl (os.flags ());
        char fc (os.fill (' '));

        // Note: the width is automatically reset to 0 (unspecified) after
        // we print the day.
        //
        os << dec << right << setw (2) << tm.tm_mday;

        os.fill (fc);
        os.flags (fl);

        ++j;       // Positions at 'e'.
        i = j + 1; // j is incremented in the for-loop header.
      }
#endif
      else
        ++j; // Skip % and the next character to handle %%.
    }
  }

  print (); // Call put_time() one last time, if required.
  return os;
}

inline ostream&
operator<< (std::ostream& os, const timestamp& ts)
{
  return to_stream (os, ts, "%Y-%m-%d %H:%M:%S%[.N]", true, true);
}

int
main (int argc, char* argv[])
{
  auto usage = [&argv] ()
  {
    cerr << "Usage:" << endl
         << "  " << argv[0] << " stat (-a|-e|-h) <file>" << endl
         << "  " << argv[0] << " iter (-p|-n) (-a|-e|-h)? <dir>" << endl;

    throw failed ();
  };

  try
  {
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

    enum class stat
    {
      none,
      attrs,
      attrs_ex,
      handle
    } st (stat::none);

    enum class iter
    {
      none,
      native,
      posix
    } it (iter::none);

    for (++i; i != argc; ++i)
    {
      string v (argv[i]);

      auto sst = [&st, &usage] (stat v)
      {
        if (st != stat::none)
          usage ();

        st = v;
      };

      auto sit = [&it, &usage] (iter v)
      {
        if (it != iter::none)
          usage ();

        it = v;
      };

      if (v == "-a")
        sst (stat::attrs);
      else if (v == "-e")
        sst (stat::attrs_ex);
      else if (v == "-h")
        sst (stat::handle);
      else if (v == "-p")
        sit (iter::posix);
      else if (v == "-n")
        sit (iter::native);
      else
        break;
    }

    auto tm = [] (const FILETIME& t) -> timestamp
    {
      // Time in FILETIME is in 100 nanosecond "ticks" since "Windows epoch"
      // (1601-01-01T00:00:00Z). To convert it to "UNIX epoch"
      // (1970-01-01T00:00:00Z) we need to subtract 11644473600 seconds.
      //
      uint64_t nsec ((static_cast<uint64_t> (t.dwHighDateTime) << 32) |
                     t.dwLowDateTime);

      nsec -= 11644473600ULL * 10000000; // Now in UNIX epoch.
      nsec *= 100;                       // Now in nanoseconds.

      return timestamp (
        chrono::duration_cast<duration> (chrono::nanoseconds (nsec)));
    };

    auto entry_tm = [&st, &tm] (const string& p) -> entry_time
    {
      switch (st)
      {
      case stat::attrs:
        {
          DWORD a (GetFileAttributesA (p.c_str ()));
          if (a == INVALID_FILE_ATTRIBUTES)
          {
            cerr << "error: GetFileAttributesA() failed for " << p << ": "
                 << last_error_msg () << endl;

            throw failed ();
          }

          return {timestamp_nonexistent, timestamp_nonexistent};
        }
      case stat::attrs_ex:
        {
          WIN32_FILE_ATTRIBUTE_DATA a;
          if (!GetFileAttributesExA (p.c_str (), GetFileExInfoStandard, &a))
          {
            cerr << "error: GetFileAttributesExA() failed for " << p
                 << ": " << last_error_msg () << endl;

            throw failed ();
          }

          return {tm (a.ftLastWriteTime), tm (a.ftLastAccessTime)};
        }
      case stat::handle:
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

          return {tm (r.ftLastWriteTime), tm (r.ftLastAccessTime)};
        }
      case stat::none: break;
      }

      assert (false); // Can't be here.
      return {timestamp_nonexistent, timestamp_nonexistent};
    };

    switch (c)
    {
    case cmd::stat:
      {
        if (i != argc - 1 || st == stat::none)
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
        timestamp start_time (system_clock::now ());

        for (string p; getline (f, p); ++count)
          entry_tm (p);

        timestamp end_time (system_clock::now ());

        if (!f.eof ())
        {
          cerr << "error: can't read " << p << endl;
          throw failed ();
        }

        duration d (end_time - start_time);
        duration de (d / count);

        cerr << "entries: " << count << endl
             << "full time: " << d << endl
             << "time per entry: " << de << endl;

        break;
      }
    case cmd::iter:
      {
        if (i != argc - 1 || it == iter::none)
          usage ();

        string p (argv[i]);

        size_t count (0);
        timestamp start_time (system_clock::now ());

        switch (it)
        {
        case iter::posix:
          {
            auto iterate = [&count] (const string& d,
                                     const auto& iterate) -> void
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

              intptr_t h (-1);
              auto_dir ad (h);

              for (;;)
              {
                bool r;
                _finddata_t fi;

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

            iterate (p, iterate);

            break;
          }

        case iter::native:
          {
            auto iterate = [&count, st, &tm, &entry_tm]
                           (const string& d, const auto& iterate) -> void
            {
              struct auto_dir
              {
                explicit
                auto_dir (HANDLE& h): h_ (&h) {}

                auto_dir (const auto_dir&) = delete;
                auto_dir& operator= (const auto_dir&) = delete;

                ~auto_dir ()
                {
                  if (h_ != nullptr && *h_ != INVALID_HANDLE_VALUE)
                    FindClose (*h_);
                }

                void release () {h_ = nullptr;}

              private:
                HANDLE* h_;
              };

              HANDLE h (INVALID_HANDLE_VALUE);
              auto_dir ad (h);

              for (;;)
              {
                bool r;
                WIN32_FIND_DATA fi;

                if (h == INVALID_HANDLE_VALUE)
                {
                  // @@ Also try FindFirstFileExA. Add tests (serial) and CI.
                  //    Can actually write test that calculates averages,
                  //    prints tables, etc... For that can add an aption that
                  //    prints nanoseconds per entry to stdout.
                  //
                  h = FindFirstFileA ((d + "\\*").c_str (), &fi);
                  r = (h != INVALID_HANDLE_VALUE);

//              cerr << "_findfirst: " << d << "\\*" << endl;
                }
                else
                {
                  r = FindNextFileA (h, &fi);

//              cerr << "_findnext" << endl;
                }

                DWORD e (GetLastError ());

                if (r)
                {
                  string p (fi.cFileName);

//              cerr << p << endl;

                  if (p == "." || p == "..")
                    continue;

                  ++count;

                  p = d + '\\' + p;

                  bool dir (
                    (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);

                  entry_time t {
                    tm (fi.ftLastWriteTime), tm (fi.ftLastAccessTime)};

                  if (st != stat::none)
                  {
                    entry_time et (entry_tm (p));

                    if (!(t.modification == et.modification &&
                          t.access <= et.access))
//                    if (t != et)
                    {
                      cerr << "error: times mismatch for " << p
                           << (dir ? "\\" : "") << endl
                           << "  find: mod " << t.modification
                           << " acc " << t.access << endl
                           << "  stat: mod " << et.modification
                           << " acc " << et.access << endl;
                      throw failed ();
                    }
                  }

                  if (dir)
                    iterate (p, iterate);
                }
                else if (e == ERROR_FILE_NOT_FOUND || e == ERROR_NO_MORE_FILES)
                {
                  // End of stream.
                  //
                  if (h != INVALID_HANDLE_VALUE)
                  {
                    FindClose (h);
                    h = INVALID_HANDLE_VALUE;
                    break;
                  }
                }
                else
                {
                  cerr << "error: Find*FileA() failed: " << error_msg (e)
                       << endl;
                  throw failed ();
                }
              }
            };

            iterate (p, iterate);

            break;
          }
        }

        timestamp end_time (system_clock::now ());

        duration d (end_time - start_time);
        duration de (d / count);

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

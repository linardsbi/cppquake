//
// Created by hvssz on 27.12.20.
//

#ifndef SDLQUAKE_1_0_9_UTIL_HPP
#define SDLQUAKE_1_0_9_UTIL_HPP

#include <string_view>
#include "quakedef.hpp"

auto getGlobalString(unsigned long offset) -> std::string_view;

auto getEdictString(unsigned long offset, edict_s *edict) -> std::string_view;

auto getGlobalStringOffsetPair(unsigned long offset) -> std::pair<unsigned, std::string>;

auto getStringByOffset(unsigned long offset) -> std::string_view;

auto getOffsetByString(std::string_view) -> unsigned long;

auto findFunctionByName(std::string_view name);

auto findFunctionByNameOffset(unsigned long offset);

auto findStringByOffset(unsigned long offset);

auto findStringByName(std::string_view str);

auto getFunctionByName(std::string_view name) -> dfunction_t;

auto getFunctionByNameOffset(unsigned long offset) -> dfunction_t;

auto getFunctionOffsetFromName(std::string_view name) -> unsigned long;

auto stringExistsAtOffset(unsigned long offset) -> bool;


/*
 * Extended streambuf class to be used with std::string_view.
 * Source: https://stackoverflow.com/a/60635270
 * Modified to avoid heap allocations.
 */
template<typename CharType, class TraitsType>
class view_streambuf final : public std::basic_streambuf<CharType, TraitsType>
{
private:
  using super_type = std::basic_streambuf<CharType, TraitsType>;
  using self_type = view_streambuf<CharType, TraitsType>;

public:
  /**
   *  These are standard types.  They permit a standardized way of
   *  referring to names of (or names dependent on) the template
   *  parameters, which are specific to the implementation.
   */
  using char_type = typename super_type::char_type;
  using traits_type = typename super_type::traits_type;
  using int_type = typename traits_type::int_type;

  using source_view = typename std::basic_string_view<char_type, traits_type>;

  view_streambuf(const source_view &src) noexcept : super_type(),
                                                    src_(src)
  {
    char_type *buff = const_cast<char_type *>(src_.data());
    this->setg(buff, buff, buff + src_.length());
  }

  std::streamsize xsgetn(char_type *__s, std::streamsize __n) override
  {
    if (0 == __n)
      return 0;
    if ((this->gptr() + __n) >= this->egptr()) {
      __n = this->egptr() - this->gptr();
      if (0 == __n && !traits_type::not_eof(this->underflow()))
        return -1;
    }
    std::memmove(static_cast<void *>(__s), this->gptr(), __n);
    this->gbump(static_cast<int>(__n));
    return __n;
  }

  int_type pbackfail(int_type __c) override
  {
    char_type *pos = this->gptr() - 1;
    *pos = traits_type::to_char_type(__c);
    this->pbump(-1);
    return 1;
  }

  int_type underflow() override
  {
    return traits_type::eof();
  }

  std::streamsize showmanyc() override
  {
    return static_cast<std::streamsize>(this->egptr() - this->gptr());
  }

  ~view_streambuf() override = default;

private:
  const source_view &src_;
};

template<typename _char_type>
class view_istream final : public std::basic_istream<_char_type, std::char_traits<_char_type>>
{
private:
  using super_type = std::basic_istream<_char_type, std::char_traits<_char_type>>;
  using streambuf_type = view_streambuf<_char_type, std::char_traits<_char_type>>;

public:
  using char_type = _char_type;
  using int_type = typename super_type::int_type;
  using pos_type = typename super_type::pos_type;
  using off_type = typename super_type::off_type;
  using traits_type = typename super_type::traits_type;
  using source_view = typename streambuf_type::source_view;

  view_istream(const view_istream &) = delete;
  view_istream &operator=(const view_istream &) = delete;

  view_istream(const source_view &src) : super_type(nullptr),
                                         sb_(streambuf_type(src))
  {
    this->init(&sb_);
  }


  view_istream(view_istream &&other) noexcept : super_type(std::forward<view_istream>(other)),
                                                sb_(std::move(other.sb_))
  {}

  view_istream &operator=(view_istream &&rhs) noexcept
  {
    view_istream(std::forward<view_istream>(rhs)).swap(*this);
    return *this;
  }

  ~view_istream() = default;

private:
  streambuf_type sb_;
};

using istringviewstream = view_istream<char>;

#endif// SDLQUAKE_1_0_9_UTIL_HPP

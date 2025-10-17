#pragma once
#include<string>
#include <utility>

#include "namespaces.hh"

//! Generic Exception thrown 
class PDNSException
{
public:
  PDNSException() : reason("Unspecified") {};
  PDNSException(string r) :
    reason(std::move(r)) {};

  string reason; //! Print this to tell the user what went wrong
};

class TimeoutException : public PDNSException
{
public:
  TimeoutException() : PDNSException() {}
  TimeoutException(const string& r) : PDNSException(r) {}
};
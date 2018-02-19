// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <signal.h>
#include <sys/epoll.h>

#pragma once
#ifndef MESH__REAL_H
#define MESH__REAL_H

#define DECLARE_REAL(name) extern decltype(::name) *name;

namespace mesh {
namespace real {
void init();

DECLARE_REAL(epoll_pwait);
DECLARE_REAL(epoll_wait);

DECLARE_REAL(sigaction);
DECLARE_REAL(sigprocmask);
};  // namespace real
};  // namespace mesh

#endif  // MESH__REAL_H

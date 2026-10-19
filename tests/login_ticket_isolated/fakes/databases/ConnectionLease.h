#pragma once
#include "databases/DatabasePool.h"
struct ConnectionLease{DatabasePool& p;explicit ConnectionLease(DatabasePool& pool):p(pool){}DatabaseConnection& connection(){return p.connection;}};

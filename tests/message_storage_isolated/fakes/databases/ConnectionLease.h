#pragma once
#include "DatabasePool.h"
class ConnectionLease {DatabasePool& pool_;public:explicit ConnectionLease(DatabasePool& p):pool_(p){}FakeConnection& connection(){return pool_.connection;}};

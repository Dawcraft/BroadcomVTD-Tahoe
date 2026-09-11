#include "../POC/RxObservation.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
using namespace bvp;
int main() {
    assert(!sparseRxPoll(0)); assert(sparseRxPoll(1)); assert(sparseRxPoll(64));
    assert(!sparseRxPoll(65)); assert(sparseRxPoll(128)); assert(!sparseRxPoll(129));
    assert(sparseRxPoll(1ULL<<63));
    assert(rxDescriptorCount(512,510,2)==4);
    assert(rxDescriptorCount(512,1,1)==0);
    assert(rxDescriptorCount(513,1,2)==0);
    assert(rxDescriptorCount(512,512,1)==0);
    assert(rxDescriptorCount(8192,1,2)==0);
    uint32_t used=0; bool limit,contended;
    for (unsigned i=0;i<RxRecordLimit;++i) {
        assert(reserveRxRecord(used,limit,contended)); assert(!limit && !contended);
    }
    assert(!reserveRxRecord(used,limit,contended) && limit && !contended);
    for (unsigned i=0;i<100000;++i) assert(!reserveRxRecord(used,limit,contended) && !limit);
    assert(used==RxRecordLimit+1);
    used=0; std::atomic<unsigned> admitted{0},limits{0}; std::vector<std::thread> workers;
    for (int i=0;i<8;++i) workers.emplace_back([&] {
        for (int j=0;j<100000;++j) {
            bool l,c; if (reserveRxRecord(used,l,c)) ++admitted; if (l) ++limits;
        }
    });
    for (auto &w:workers) w.join();
    assert(admitted==RxRecordLimit && limits==1 && used==RxRecordLimit+1);
    puts("PASS: RX observation sparse sampling, validated ring bounds, saturating nonblocking budget and concurrency");
}

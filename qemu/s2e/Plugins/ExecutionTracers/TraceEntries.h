#ifndef S2E_PLUGINS_TRACEENTRIES_H
#define S2E_PLUGINS_TRACEENTRIES_H

#include <inttypes.h>
#include <string.h>
#include <string>
#include <vector>

namespace s2e {
namespace plugins {

enum ExecTraceEntryType {
    TRACE_MOD_LOAD = 0,
    TRACE_MOD_UNLOAD,
    TRACE_PROC_UNLOAD,
    TRACE_CALL, TRACE_RET,
    TRACE_TB_START,
    TRACE_TB_END,
    TRACE_MODULE_DESC,
    TRACE_FORK,
    TRACE_CACHESIM,
    TRACE_TESTCASE,
    TRACE_MAX
};


struct ExecutionTraceItemHeader{
    uint64_t timeStamp;
    uint8_t  size;  //Size of the payload
    uint8_t  type;
    uint32_t stateId;
    uint64_t pid;
    //uint8_t  payload[];
}__attribute__((packed));

struct ExecutionTraceModuleLoad {
    char name[32];
    uint64_t loadBase;
    uint64_t nativeBase;
    uint64_t size;
}__attribute__((packed));

struct ExecutionTraceModuleUnload {
    uint64_t loadBase;
}__attribute__((packed));

struct ExecutionTraceProcessUnload {

}__attribute__((packed));


struct ExecutionTraceCall {
    //These are absolute addresses
    uint64_t source, target;
}__attribute__((packed));

struct ExecutionTraceReturn {
    //These are absolute addresses
    uint64_t source, target;
}__attribute__((packed));

struct ExecutionTraceFork {
    uint64_t pc;
    uint32_t stateCount;
    //Array of states (uint32_t)...
    uint32_t children[1];
}__attribute__((packed));


enum CacheSimDescType{
    CACHE_PARAMS=0,
    CACHE_NAME,
    CACHE_ENTRY
};

struct ExecutionTraceCacheSimParams {
    uint8_t type;
    uint32_t cacheId;
    uint32_t size;
    uint32_t lineSize;
    uint32_t associativity;
    uint32_t upperCacheId;
}__attribute__((packed));

struct ExecutionTraceCacheSimName {
    uint8_t type;
    uint32_t id;
    //XXX: make sure it does not overflow the overall entry size
    uint32_t length;
    uint8_t  name[1];

    //XXX: should use placement new operator instead
    static ExecutionTraceCacheSimName *allocate(
            uint32_t id, const std::string &str, uint32_t *retsize) {
        unsigned size = sizeof(ExecutionTraceCacheSimName) +
                        str.size();
        uint8_t *a = new uint8_t[size];
        ExecutionTraceCacheSimName *ret = (ExecutionTraceCacheSimName*)a;
        strcpy((char*)ret->name, str.c_str());
        ret->length = str.size();
        ret->id = id;
        ret->type = CACHE_NAME;
        *retsize = size;
        return ret;
    }
    static void deallocate(ExecutionTraceCacheSimName *o) {
        delete [] (uint8_t *)o;
    }

}__attribute__((packed));

struct ExecutionTraceCacheSimEntry {
    uint8_t type;
    uint8_t cacheId;
    uint64_t pc, address;
    uint8_t size;
    uint8_t isWrite, isCode, missCount;
    //XXX: should find a compact way of encoding caches.
}__attribute__((packed));

union ExecutionTraceCache {
    uint8_t type;
    ExecutionTraceCacheSimParams params;
    ExecutionTraceCacheSimName name;
    ExecutionTraceCacheSimEntry entry;
}__attribute__((packed));

struct ExecutionTraceTestCase {
    struct Header {
        uint32_t nameSize;
        uint32_t dataSize;
    }__attribute__((packed));

    typedef std::pair<std::string, std::vector<unsigned char> > VarValuePair;
    typedef std::vector<VarValuePair> ConcreteInputs;
    static ExecutionTraceTestCase *serialize(unsigned *size, const ConcreteInputs &inputs) {
        unsigned bufsize=0;
        ConcreteInputs::const_iterator it;
        for(it = inputs.begin(); it != inputs.end(); ++it) {
            bufsize += sizeof(Header) + (*it).first.size() + (*it).second.size();
        }

        uint8_t *a = new uint8_t[bufsize];
        ExecutionTraceTestCase *ret = (ExecutionTraceTestCase*)a;

        for(it = inputs.begin(); it != inputs.end(); ++it) {
            Header hdr = {(*it).first.size(), (*it).second.size()};
            memcpy(a, &hdr, sizeof(hdr));
            a+=sizeof(hdr);
            memcpy(a, (*it).first.c_str(), (*it).first.size());
            a+=(*it).first.size();
            for (unsigned i=0; i<(*it).second.size(); ++i, ++a) {
                *a = (*it).second[i];
            }
        }
        *size = bufsize;
        return ret;
    }

    static void deserialize(void *buf, size_t buflen, ConcreteInputs &out) {
        uint8_t *a = (uint8_t*)buf;
        while(buflen > 0) {
            Header *hdr = (Header*)a;
            a+=sizeof(*hdr);
            buflen -= sizeof(*hdr);
            out.push_back(VarValuePair("", std::vector<unsigned char>()));
            VarValuePair &vp = out.back();
            for (unsigned i=0; i<hdr->nameSize; ++i, a++, --buflen) {
                vp.first += (char) *a;
            }
            for (unsigned i=0; i<hdr->dataSize; ++i, a++, --buflen) {
                vp.second.push_back((char) *a);
            }
        }
    }

    static void deallocate(ExecutionTraceTestCase *o) {
        delete [] (uint8_t *)o;
    }


};

union ExecutionTraceAll {
    ExecutionTraceModuleLoad moduleLoad;
    ExecutionTraceModuleUnload moduleUnload;
    ExecutionTraceCall call;
    ExecutionTraceReturn ret;
}__attribute__((packed));

}
}
#endif
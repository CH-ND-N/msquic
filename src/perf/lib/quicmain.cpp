/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    QUIC Perf Main execution engine.

--*/

#include "PerfHelpers.h"
#include "PerfServer.h"
#include "ThroughputClient.h"
#include "RpsClient.h"
#include "HpsClient.h"

#ifdef QUIC_CLOG
#include "quicmain.cpp.clog.h"
#endif

const MsQuicApi* MsQuic;
volatile int BufferCurrent;
char Buffer[BufferLength];

PerfBase* TestToRun;

#include "quic_datapath.h"

QUIC_DATAPATH_RECEIVE_CALLBACK DatapathReceive;
QUIC_DATAPATH_UNREACHABLE_CALLBACK DatapathUnreachable;
QUIC_DATAPATH* Datapath;
QUIC_DATAPATH_BINDING* Binding;
bool ServerMode = false;

static
void
PrintHelp(
    ) {
    WriteOutput(
        "\n"
        "quicperf usage:\n"
        "\n"
        "Server: quicperf [options]\n"
        "\n"
        "  -port:<####>                The UDP port of the server. (def:%u)\n"
        "  -selfsign:<0/1>             Uses a self-signed server certificate.\n"
        "  -thumbprint:<cert_hash>     The hash or thumbprint of the certificate to use.\n"
        "  -cert_store:<store name>    The certificate store to search for the thumbprint in.\n"
        "  -machine_cert:<0/1>         Use the machine, or current user's, certificate store. (def:0)\n"
        "\n"
        "Client: quicperf -TestName:<Throughput|RPS|HPS> [options]\n"
        "\n",
        PERF_DEFAULT_PORT
        );
}

static
void
DumpMsQuicPerfCountersToOutput(
    _In_ const QUIC_API_TABLE* MsQuic
    )
{
    uint64_t Counters[QUIC_PERF_COUNTER_MAX] = {0};
    uint32_t Lenth = sizeof(Counters);
    MsQuic->GetParam(
        NULL,
        QUIC_PARAM_LEVEL_GLOBAL,
        QUIC_PARAM_GLOBAL_PERF_COUNTERS,
        &Lenth,
        &Counters);
    WriteOutput("Perf Counters:\n");
    WriteOutput("  CONN_CREATED:          %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_CREATED]);
    WriteOutput("  CONN_HANDSHAKE_FAIL:   %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_HANDSHAKE_FAIL]);
    WriteOutput("  CONN_APP_REJECT:       %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_APP_REJECT]);
    WriteOutput("  CONN_ACTIVE:           %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_ACTIVE]);
    WriteOutput("  CONN_CONNECTED:        %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_CONNECTED]);
    WriteOutput("  CONN_PROTOCOL_ERRORS:  %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_PROTOCOL_ERRORS]);
    WriteOutput("  CONN_NO_ALPN:          %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_NO_ALPN]);
    WriteOutput("  STRM_ACTIVE:           %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_STRM_ACTIVE]);
    WriteOutput("  PKTS_SUSPECTED_LOST:   %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_PKTS_SUSPECTED_LOST]);
    WriteOutput("  PKTS_DROPPED:          %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_PKTS_DROPPED]);
    WriteOutput("  PKTS_DECRYPTION_FAIL:  %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_PKTS_DECRYPTION_FAIL]);
    WriteOutput("  UDP_RECV:              %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_UDP_RECV]);
    WriteOutput("  UDP_SEND:              %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_UDP_SEND]);
    WriteOutput("  UDP_RECV_BYTES:        %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_UDP_RECV_BYTES]);
    WriteOutput("  UDP_SEND_BYTES:        %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_UDP_SEND_BYTES]);
    WriteOutput("  UDP_RECV_EVENTS:       %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_UDP_RECV_EVENTS]);
    WriteOutput("  UDP_SEND_CALLS:        %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_UDP_SEND_CALLS]);
    WriteOutput("  APP_SEND_BYTES:        %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_APP_SEND_BYTES]);
    WriteOutput("  APP_RECV_BYTES:        %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_APP_RECV_BYTES]);
    WriteOutput("  CONN_QUEUE_DEPTH:      %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_QUEUE_DEPTH]);
    WriteOutput("  CONN_OPER_QUEUE_DEPTH: %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_OPER_QUEUE_DEPTH]);
    WriteOutput("  CONN_OPER_QUEUED:      %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_OPER_QUEUED]);
    WriteOutput("  CONN_OPER_COMPLETED:   %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_CONN_OPER_COMPLETED]);
    WriteOutput("  WORK_OPER_QUEUE_DEPTH: %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_WORK_OPER_QUEUE_DEPTH]);
    WriteOutput("  WORK_OPER_QUEUED:      %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_WORK_OPER_QUEUED]);
    WriteOutput("  WORK_OPER_COMPLETED:   %llu\n", (unsigned long long)Counters[QUIC_PERF_COUNTER_WORK_OPER_COMPLETED]);
}

QUIC_STATUS
QuicMainStart(
    _In_ int argc,
    _In_reads_(argc) _Null_terminated_ char* argv[],
    _In_ QUIC_EVENT* StopEvent,
    _In_ const QUIC_CREDENTIAL_CONFIG* SelfSignedCredConfig
    ) {
    argc--; argv++; // Skip app name

    if (argc == 0 || IsArg(argv[0], "?") || IsArg(argv[0], "help")) {
        PrintHelp();
        return QUIC_STATUS_INVALID_PARAMETER;
    }

    const char* TestName = GetValue(argc, argv, "test");
    ServerMode = TestName == nullptr;

    QUIC_STATUS Status;

    if (ServerMode) {
        Datapath = nullptr;
        Binding = nullptr;
        Status = QuicDataPathInitialize(0, DatapathReceive, DatapathUnreachable, &Datapath);
        if (QUIC_FAILED(Status)) {
            WriteOutput("Datapath for shutdown failed to initialize: %d\n", Status);
            return Status;
        }

        QuicAddr LocalAddress {QUIC_ADDRESS_FAMILY_INET, (uint16_t)9999};
        Status = QuicDataPathBindingCreate(Datapath, &LocalAddress.SockAddr, nullptr, StopEvent, &Binding);
        if (QUIC_FAILED(Status)) {
            QuicDataPathUninitialize(Datapath);
            WriteOutput("Datapath Binding for shutdown failed to initialize: %d\n", Status);
            return Status;
        }
    }

    MsQuic = new(std::nothrow) MsQuicApi;
    if (MsQuic == nullptr) {
        WriteOutput("MsQuic Alloc Out of Memory\n");
        return QUIC_STATUS_OUT_OF_MEMORY;
    }
    if (QUIC_FAILED(Status = MsQuic->GetInitStatus())) {
        delete MsQuic;
        MsQuic = nullptr;
        WriteOutput("MsQuic Failed To Initialize: %d\n", Status);
        return Status;
    }

    if (ServerMode) {
        TestToRun = new(std::nothrow) PerfServer(SelfSignedCredConfig);
    } else {
        if (IsValue(TestName, "Throughput") || IsValue(TestName, "tput")) {
            TestToRun = new(std::nothrow) ThroughputClient;
        } else if (IsValue(TestName, "RPS")) {
            TestToRun = new(std::nothrow) RpsClient;
        } else if (IsValue(TestName, "HPS")) {
            TestToRun = new(std::nothrow) HpsClient;
        } else {
            PrintHelp();
            delete MsQuic;
            return QUIC_STATUS_INVALID_PARAMETER;
        }
    }

    if (TestToRun != nullptr) {
        Status = TestToRun->Init(argc, argv);
        if (QUIC_SUCCEEDED(Status)) {
            Status = TestToRun->Start(StopEvent);
            if (QUIC_SUCCEEDED(Status)) {
                return QUIC_STATUS_SUCCESS;
            } else {
                WriteOutput("Test Failed To Start: %d\n", Status);
            }
        } else {
            WriteOutput("Test Failed To Initialize: %d\n", Status);
        }
    } else {
        WriteOutput("Test Alloc Out Of Memory\n");
        Status = QUIC_STATUS_OUT_OF_MEMORY;
    }

    delete TestToRun;
    TestToRun = nullptr;
    delete MsQuic;
    MsQuic = nullptr;
    return Status;
}

QUIC_STATUS
QuicMainStop(
    _In_ int Timeout
    ) {
    if (TestToRun == nullptr) {
        if (ServerMode) {
            QuicDataPathBindingDelete(Binding);
            QuicDataPathUninitialize(Datapath);
            Datapath = nullptr;
            Binding = nullptr;
        }
        return QUIC_STATUS_SUCCESS;
    }

    QUIC_STATUS Status = TestToRun->Wait(Timeout);
    DumpMsQuicPerfCountersToOutput(MsQuic);
    delete TestToRun;
    delete MsQuic;
    if (ServerMode) {
        QuicDataPathBindingDelete(Binding);
        QuicDataPathUninitialize(Datapath);
        Datapath = nullptr;
        Binding = nullptr;
    }
    MsQuic = nullptr;
    TestToRun = nullptr;
    return Status;
}

void
DatapathReceive(
    _In_ QUIC_DATAPATH_BINDING*,
    _In_ void* Context,
    _In_ QUIC_RECV_DATAGRAM*
    )
{
    QUIC_EVENT* Event = static_cast<QUIC_EVENT*>(Context);
    QuicEventSet(*Event);
}

void
DatapathUnreachable(
    _In_ QUIC_DATAPATH_BINDING*,
    _In_ void*,
    _In_ const QUIC_ADDR*
    )
{
    //
    // Do nothing, we never send
    //
}

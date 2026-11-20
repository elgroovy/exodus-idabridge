#ifndef __IDABRIDGE_DBGSERVER_H__
#define __IDABRIDGE_DBGSERVER_H__
// grpc needs Vista headers but Exodus pins _WIN32_WINNT to XP, so windows.h is pulled in here first
#define NOMINMAX
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <windows.h>
#include <grpcpp/grpcpp.h>
#include "debug_proto_68k.grpc.pb.h"
#undef _WIN32_WINNT
#include "ExtensionInterface/ExtensionInterface.pkg"
#include "Processor/Processor.pkg"
#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
class IM68000;
class IS315_5313;
class ITimedBufferInt;

// Emulator side of the smd_ida_tools2 debugger protocol
class DbgServerImpl :public idadebug::DbgServer::Service
{
public:
	// Constructors
	DbgServerImpl(ISystemExtensionInterface& system, int eventPort);
	~DbgServerImpl();

	// Register functions
	virtual grpc::Status get_gp_reg(grpc::ServerContext* context, const idadebug::GpReg* request, idadebug::AnyRegValue* response);
	virtual grpc::Status get_gp_regs(grpc::ServerContext* context, const google::protobuf::Empty* request, idadebug::GpRegs* response);
	virtual grpc::Status set_gp_reg(grpc::ServerContext* context, const idadebug::GpRegValue* request, google::protobuf::Empty* response);
	virtual grpc::Status get_vdp_reg(grpc::ServerContext* context, const idadebug::VdpReg* request, idadebug::AnyRegValue* response);
	virtual grpc::Status get_vdp_regs(grpc::ServerContext* context, const google::protobuf::Empty* request, idadebug::VdpRegs* response);
	virtual grpc::Status set_vdp_reg(grpc::ServerContext* context, const idadebug::VdpRegValue* request, google::protobuf::Empty* response);
	virtual grpc::Status get_dma_info(grpc::ServerContext* context, const google::protobuf::Empty* request, idadebug::DmaInfo* response);

	// Memory functions
	virtual grpc::Status read_memory(grpc::ServerContext* context, const idadebug::MemoryAS* request, idadebug::MemData* response);
	virtual grpc::Status write_memory(grpc::ServerContext* context, const idadebug::MemoryAD* request, google::protobuf::Empty* response);

	// Breakpoint functions
	virtual grpc::Status get_breakpoints(grpc::ServerContext* context, const google::protobuf::Empty* request, idadebug::DbgBreakpoints* response);
	virtual grpc::Status add_breakpoint(grpc::ServerContext* context, const idadebug::DbgBreakpoint* request, google::protobuf::Empty* response);
	virtual grpc::Status toggle_breakpoint(grpc::ServerContext* context, const idadebug::DbgBreakpoint* request, google::protobuf::Empty* response);
	virtual grpc::Status update_breakpoint(grpc::ServerContext* context, const idadebug::DbgBreakpoint* request, google::protobuf::Empty* response);
	virtual grpc::Status del_breakpoint(grpc::ServerContext* context, const idadebug::DbgBreakpoint* request, google::protobuf::Empty* response);
	virtual grpc::Status clear_breakpoints(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);

	// Execution functions
	virtual grpc::Status pause(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);
	virtual grpc::Status resume(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);
	virtual grpc::Status start_emulation(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);
	virtual grpc::Status exit_emulation(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);
	virtual grpc::Status step_into(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);
	virtual grpc::Status step_over(grpc::ServerContext* context, const google::protobuf::Empty* request, google::protobuf::Empty* response);
	virtual grpc::Status get_callstack(grpc::ServerContext* context, const google::protobuf::Empty* request, idadebug::Callstack* response);

private:
	struct BreakpointEntry
	{
		idadebug::DbgBreakpoint info;
		IBreakpoint* breakpoint = 0;
		IWatchpoint* watchpoint = 0;
	};

private:
	// Device functions
	IDevice* GetM68000Device() const;
	IM68000* GetM68000() const;
	IProcessor* GetProcessor() const;
	IS315_5313* GetVdp() const;

	// Memory functions
	ITimedBufferInt* GetVdpBuffer(unsigned int address, unsigned int& offset) const;

	// Breakpoint functions
	std::list<BreakpointEntry>::iterator FindBreakpoint(const idadebug::DbgBreakpoint& request);
	void CreateExodusBreakpoint(BreakpointEntry& entry);
	void DeleteExodusBreakpoint(BreakpointEntry& entry);
	void SetExodusBreakpointEnabled(BreakpointEntry& entry, bool state);
	void SyncVdpBreakpoints();

	// Event functions
	void MonitorThread();
	bool ShouldBreak();
	bool EvalCondition(const idadebug::DbgBreakpoint& info);
	void SendPauseEvent();
	void SendStopEvent();

	// Trace functions
	void StartTrace();
	void StopTrace();
	void DrainTrace();
	void TakeChanged(google::protobuf::Map<uint32_t, uint32_t>& changed);

private:
	static const unsigned int VdpMemoryBase = 0xD00000;
	static const unsigned int MaxRomSize = 0x800000;
	static const unsigned int TraceLength = 0x10000;

	ISystemExtensionInterface& _system;
	int _eventPort;

	std::mutex _breakpointMutex;
	std::list<BreakpointEntry> _breakpoints;

	std::mutex _executeMutex;
	std::mutex _clientMutex;
	std::unique_ptr<idadebug::DbgClient::Stub> _client;

	std::atomic<bool> _running;
	std::atomic<bool> _stopRequested;
	std::atomic<bool> _stopMonitor;
	std::thread _monitorThread;

	std::mutex _traceMutex;
	bool _tracing;
	bool _savedTraceEnabled;
	bool _savedTraceDisassemble;
	unsigned int _savedTraceLength;
	unsigned int _lastTracePc;
	std::map<unsigned int, unsigned int> _changed;
};

#endif

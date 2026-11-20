#include "DbgServer.h"
#include "M68000/IM68000.h"
#include "315-5313/IS315_5313.h"

using grpc::ServerContext;
using grpc::Status;
using google::protobuf::Empty;
using namespace idadebug;

//----------------------------------------------------------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------------------------------------------------------
DbgServerImpl::DbgServerImpl(ISystemExtensionInterface& system, int eventPort)
:_system(system), _eventPort(eventPort), _running(false), _stopRequested(false), _stopMonitor(false),
 _tracing(false), _savedTraceEnabled(false), _savedTraceDisassemble(false), _savedTraceLength(0), _lastTracePc(0)
{
	_monitorThread = std::thread(&DbgServerImpl::MonitorThread, this);
}

//----------------------------------------------------------------------------------------------------------------------
DbgServerImpl::~DbgServerImpl()
{
	_stopMonitor = true;
	if (_monitorThread.joinable())
	{
		_monitorThread.join();
	}
	StopTrace();

	std::unique_lock<std::mutex> lock(_breakpointMutex);
	for (auto& entry : _breakpoints)
	{
		DeleteExodusBreakpoint(entry);
	}
	_breakpoints.clear();
	SyncVdpBreakpoints();
}

//----------------------------------------------------------------------------------------------------------------------
// Device functions
//----------------------------------------------------------------------------------------------------------------------
IDevice* DbgServerImpl::GetM68000Device() const
{
	std::list<IDevice*> devices = _system.GetLoadedDevices();
	for (IDevice* device : devices)
	{
		if (dynamic_cast<IM68000*>(device) != 0)
		{
			return device;
		}
	}
	return 0;
}

//----------------------------------------------------------------------------------------------------------------------
IM68000* DbgServerImpl::GetM68000() const
{
	return dynamic_cast<IM68000*>(GetM68000Device());
}

//----------------------------------------------------------------------------------------------------------------------
IProcessor* DbgServerImpl::GetProcessor() const
{
	return dynamic_cast<IProcessor*>(GetM68000());
}

//----------------------------------------------------------------------------------------------------------------------
IS315_5313* DbgServerImpl::GetVdp() const
{
	std::list<IDevice*> devices = _system.GetLoadedDevices();
	for (IDevice* device : devices)
	{
		IS315_5313* vdp = dynamic_cast<IS315_5313*>(device);
		if (vdp != 0)
		{
			return vdp;
		}
	}
	return 0;
}

//----------------------------------------------------------------------------------------------------------------------
// Register functions
//----------------------------------------------------------------------------------------------------------------------
static bool ReadGpReg(IM68000& cpu, GpRegsEnum reg, unsigned int& value)
{
	if ((reg >= D0) && (reg <= D7))
	{
		value = cpu.GetD(reg - D0);
	}
	else if ((reg >= A0) && (reg <= A7))
	{
		value = cpu.GetA((reg - A0) / 2);
	}
	else if (reg == PC)
	{
		value = cpu.GetPC() & 0xFFFFFF;
	}
	else if (reg == SP)
	{
		value = cpu.GetA(IM68000::SP);
	}
	else if (reg == SR)
	{
		value = cpu.GetSR();
	}
	else
	{
		return false;
	}
	return true;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_gp_reg(ServerContext*, const GpReg* request, AnyRegValue* response)
{
	IM68000* cpu = GetM68000();
	unsigned int value = 0;
	if ((cpu == 0) || !ReadGpReg(*cpu, request->reg(), value))
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}
	response->set_value(value);
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_gp_regs(ServerContext*, const Empty*, GpRegs* response)
{
	IM68000* cpu = GetM68000();
	if (cpu == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	response->set_d0(cpu->GetD(0));
	response->set_d1(cpu->GetD(1));
	response->set_d2(cpu->GetD(2));
	response->set_d3(cpu->GetD(3));
	response->set_d4(cpu->GetD(4));
	response->set_d5(cpu->GetD(5));
	response->set_d6(cpu->GetD(6));
	response->set_d7(cpu->GetD(7));

	response->set_a0(cpu->GetA(0));
	response->set_a1(cpu->GetA(1));
	response->set_a2(cpu->GetA(2));
	response->set_a3(cpu->GetA(3));
	response->set_a4(cpu->GetA(4));
	response->set_a5(cpu->GetA(5));
	response->set_a6(cpu->GetA(6));
	response->set_a7(cpu->GetA(7));
	response->set_sp(cpu->GetA(IM68000::SP));

	response->set_pc(cpu->GetPC() & 0xFFFFFF);
	response->set_sr(cpu->GetSR());
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::set_gp_reg(ServerContext*, const GpRegValue* request, Empty*)
{
	IM68000* cpu = GetM68000();
	if (cpu == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	GpRegsEnum reg = request->index();
	unsigned int value = request->value();
	if ((reg >= D0) && (reg <= D7))
	{
		cpu->SetD(reg - D0, value);
	}
	else if ((reg >= A0) && (reg <= A7))
	{
		cpu->SetA((reg - A0) / 2, value);
	}
	else if (reg == PC)
	{
		cpu->SetPC(value & 0xFFFFFF);
	}
	else if (reg == SP)
	{
		cpu->SetA(IM68000::SP, value);
	}
	else if (reg == SR)
	{
		cpu->SetSR(value & 0xFFFF);
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_vdp_reg(ServerContext*, const VdpReg* request, AnyRegValue* response)
{
	IS315_5313* vdp = GetVdp();
	if (vdp == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no VDP");
	}
	if ((unsigned int)request->reg() < IS315_5313::RegisterCount)
	{
		response->set_value(vdp->GetRegisterData(request->reg()));
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_vdp_regs(ServerContext*, const Empty*, VdpRegs* response)
{
	IS315_5313* vdp = GetVdp();
	if (vdp == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no VDP");
	}

	response->set_v00(vdp->GetRegisterData(0x00));
	response->set_v01(vdp->GetRegisterData(0x01));
	response->set_v02(vdp->GetRegisterData(0x02));
	response->set_v03(vdp->GetRegisterData(0x03));
	response->set_v04(vdp->GetRegisterData(0x04));
	response->set_v05(vdp->GetRegisterData(0x05));
	response->set_v06(vdp->GetRegisterData(0x06));
	response->set_v07(vdp->GetRegisterData(0x07));
	response->set_v08(vdp->GetRegisterData(0x08));
	response->set_v09(vdp->GetRegisterData(0x09));
	response->set_v0a(vdp->GetRegisterData(0x0A));
	response->set_v0b(vdp->GetRegisterData(0x0B));
	response->set_v0c(vdp->GetRegisterData(0x0C));
	response->set_v0d(vdp->GetRegisterData(0x0D));
	response->set_v0e(vdp->GetRegisterData(0x0E));
	response->set_v0f(vdp->GetRegisterData(0x0F));
	response->set_v10(vdp->GetRegisterData(0x10));
	response->set_v11(vdp->GetRegisterData(0x11));
	response->set_v12(vdp->GetRegisterData(0x12));
	response->set_v13(vdp->GetRegisterData(0x13));
	response->set_v14(vdp->GetRegisterData(0x14));
	response->set_v15(vdp->GetRegisterData(0x15));
	response->set_v16(vdp->GetRegisterData(0x16));
	response->set_v17(vdp->GetRegisterData(0x17));
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::set_vdp_reg(ServerContext*, const VdpRegValue* request, Empty*)
{
	IS315_5313* vdp = GetVdp();
	if (vdp == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no VDP");
	}
	if ((unsigned int)request->index() < IS315_5313::RegisterCount)
	{
		vdp->SetRegisterData(request->index(), request->value() & 0xFF);
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_dma_info(ServerContext*, const Empty*, DmaInfo* response)
{
	IS315_5313* vdp = GetVdp();
	if (vdp == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no VDP");
	}

	response->set_len((vdp->GetRegisterData(0x13) & 0xFF) | ((vdp->GetRegisterData(0x14) & 0xFF) << 8));

	unsigned int high = vdp->GetRegisterData(0x17);
	unsigned int src = (vdp->GetRegisterData(0x15) & 0xFF) | ((vdp->GetRegisterData(0x16) & 0xFF) << 8);
	src |= (high & ((high & 0x80) ? 0x3F : 0x7F)) << 16;
	response->set_src((src << 1) & 0xFFFFFF);

	// Same fake VRAM/CRAM/VSRAM addresses as read_memory
	unsigned int dst = VdpMemoryBase;
	switch (vdp->RegGetPortCode() & 0x0F)
	{
	case 0x1:
		dst = VdpMemoryBase + 0x00000 + (vdp->RegGetPortAddress() & 0xFFFF);
		break;
	case 0x3:
		dst = VdpMemoryBase + 0x10000 + (vdp->RegGetPortAddress() & 0xFFFF);
		break;
	case 0x5:
		dst = VdpMemoryBase + 0x20000 + (vdp->RegGetPortAddress() & 0xFFFF);
		break;
	}
	response->set_dst(dst);
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
// Memory functions
//----------------------------------------------------------------------------------------------------------------------
ITimedBufferInt* DbgServerImpl::GetVdpBuffer(unsigned int address, unsigned int& offset) const
{
	// Gens puts VRAM, CRAM and VSRAM at 0xD00000, 0xD10000 and 0xD20000 for IDA
	if ((address < VdpMemoryBase) || (address >= VdpMemoryBase + 0x30000))
	{
		return 0;
	}
	IS315_5313* vdp = GetVdp();
	if (vdp == 0)
	{
		return 0;
	}

	offset = (address - VdpMemoryBase) & 0xFFFF;
	switch ((address - VdpMemoryBase) >> 16)
	{
	case 0:
		return vdp->GetVRAMBuffer();
	case 1:
		return vdp->GetCRAMBuffer();
	default:
		return vdp->GetVSRAMBuffer();
	}
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::read_memory(ServerContext*, const MemoryAS* request, MemData* response)
{
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	std::string* data = response->mutable_data();
	data->reserve(request->size());
	for (unsigned int i = 0; i < request->size(); ++i)
	{
		unsigned int address = request->address() + i;
		unsigned int offset = 0;
		unsigned char value = 0;
		ITimedBufferInt* buffer = GetVdpBuffer(address, offset);
		if (buffer != 0)
		{
			value = (offset < buffer->Size()) ? buffer->ReadLatest(offset) : 0;
		}
		else if (address <= 0xFFFFFF)
		{
			value = (unsigned char)cpu->GetMemorySpaceByte(address);
		}
		data->push_back((char)value);
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::write_memory(ServerContext*, const MemoryAD* request, Empty*)
{
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	const std::string& data = request->data();
	for (size_t i = 0; i < data.size(); ++i)
	{
		unsigned int address = request->address() + (unsigned int)i;
		unsigned char value = (unsigned char)data[i];
		unsigned int offset = 0;
		ITimedBufferInt* buffer = GetVdpBuffer(address, offset);
		if (buffer != 0)
		{
			if (offset < buffer->Size())
			{
				buffer->WriteLatest(offset, value);
			}
		}
		else if (address <= 0xFFFFFF)
		{
			cpu->SetMemorySpaceByte(address, value);
		}
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
// Breakpoint functions
//----------------------------------------------------------------------------------------------------------------------
std::list<DbgServerImpl::BreakpointEntry>::iterator DbgServerImpl::FindBreakpoint(const DbgBreakpoint& request)
{
	for (auto i = _breakpoints.begin(); i != _breakpoints.end(); ++i)
	{
		if ((i->info.type() == request.type()) && (i->info.bstart() == request.bstart()) && (i->info.is_vdp() == request.is_vdp()))
		{
			return i;
		}
	}
	return _breakpoints.end();
}

//----------------------------------------------------------------------------------------------------------------------
template<class T> static void SetLocation(T* point, unsigned int start, unsigned int end)
{
	// Exodus range conditions are exclusive at both ends
	if (start == end)
	{
		point->SetLocationCondition(T::Condition::Equal);
		point->SetLocationConditionData1(start);
	}
	else
	{
		point->SetLocationCondition(T::Condition::GreaterAndLess);
		point->SetLocationConditionData1(start - 1);
		point->SetLocationConditionData2(end + 1);
	}
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::SyncVdpBreakpoints()
{
	// Memory breakpoints were added to the VDP in interface version 2
	IS315_5313* vdp = GetVdp();
	if ((vdp == 0) || (vdp->GetIS315_5313Version() < 2))
	{
		return;
	}

	vdp->ClearMemoryBreakpoints();
	for (const auto& entry : _breakpoints)
	{
		if (entry.info.is_vdp() && entry.info.enabled() && (entry.info.type() != BP_PC))
		{
			vdp->AddMemoryBreakpoint(entry.info.bstart(), entry.info.bend(), entry.info.type() == BP_READ, entry.info.type() == BP_WRITE);
		}
	}
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::CreateExodusBreakpoint(BreakpointEntry& entry)
{
	IProcessor* cpu = GetProcessor();
	if ((cpu == 0) || entry.info.is_vdp())
	{
		return;
	}

	unsigned int start = entry.info.bstart() & 0xFFFFFF;
	unsigned int end = entry.info.bend() & 0xFFFFFF;
	if (entry.info.type() == BP_PC)
	{
		IBreakpoint* breakpoint = cpu->CreateBreakpoint();
		if (cpu->LockBreakpoint(breakpoint))
		{
			SetLocation(breakpoint, start, end);
			breakpoint->SetBreakEvent(true);
			std::wstring name = breakpoint->GenerateName();
			breakpoint->SetName(L"IDA " + name);
			breakpoint->SetEnabled(entry.info.enabled());
			cpu->UnlockBreakpoint(breakpoint);
		}
		entry.breakpoint = breakpoint;
	}
	else if ((entry.info.type() == BP_READ) || (entry.info.type() == BP_WRITE))
	{
		IWatchpoint* watchpoint = cpu->CreateWatchpoint();
		if (cpu->LockWatchpoint(watchpoint))
		{
			SetLocation(watchpoint, start, end);
			watchpoint->SetOnRead(entry.info.type() == BP_READ);
			watchpoint->SetOnWrite(entry.info.type() == BP_WRITE);
			watchpoint->SetBreakEvent(true);
			std::wstring name = watchpoint->GenerateName();
			watchpoint->SetName(L"IDA " + name);
			watchpoint->SetEnabled(entry.info.enabled());
			cpu->UnlockWatchpoint(watchpoint);
		}
		entry.watchpoint = watchpoint;
	}
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::DeleteExodusBreakpoint(BreakpointEntry& entry)
{
	IProcessor* cpu = GetProcessor();
	if (cpu != 0)
	{
		if (entry.breakpoint != 0)
		{
			cpu->DeleteBreakpoint(entry.breakpoint);
		}
		if (entry.watchpoint != 0)
		{
			cpu->DeleteWatchpoint(entry.watchpoint);
		}
	}
	entry.breakpoint = 0;
	entry.watchpoint = 0;
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::SetExodusBreakpointEnabled(BreakpointEntry& entry, bool state)
{
	entry.info.set_enabled(state);
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return;
	}
	if ((entry.breakpoint != 0) && cpu->LockBreakpoint(entry.breakpoint))
	{
		entry.breakpoint->SetEnabled(state);
		cpu->UnlockBreakpoint(entry.breakpoint);
	}
	if ((entry.watchpoint != 0) && cpu->LockWatchpoint(entry.watchpoint))
	{
		entry.watchpoint->SetEnabled(state);
		cpu->UnlockWatchpoint(entry.watchpoint);
	}
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_breakpoints(ServerContext*, const Empty*, DbgBreakpoints* response)
{
	std::unique_lock<std::mutex> lock(_breakpointMutex);
	for (const auto& entry : _breakpoints)
	{
		*response->add_list() = entry.info;
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::add_breakpoint(ServerContext*, const DbgBreakpoint* request, Empty*)
{
	std::unique_lock<std::mutex> lock(_breakpointMutex);
	BreakpointEntry entry;
	entry.info = *request;
	entry.info.set_bstart(request->bstart() & 0xFFFFFF);
	entry.info.set_bend(request->bend() & 0xFFFFFF);
	entry.info.set_enabled(true);
	CreateExodusBreakpoint(entry);
	_breakpoints.push_back(entry);
	SyncVdpBreakpoints();
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::toggle_breakpoint(ServerContext*, const DbgBreakpoint* request, Empty*)
{
	std::unique_lock<std::mutex> lock(_breakpointMutex);
	auto i = FindBreakpoint(*request);
	if (i != _breakpoints.end())
	{
		SetExodusBreakpointEnabled(*i, !i->info.enabled());
	}
	SyncVdpBreakpoints();
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::update_breakpoint(ServerContext*, const DbgBreakpoint* request, Empty*)
{
	std::unique_lock<std::mutex> lock(_breakpointMutex);
	auto i = FindBreakpoint(*request);
	if (i != _breakpoints.end())
	{
		i->info.set_elang(request->elang());
		i->info.set_condition(request->condition());
		SetExodusBreakpointEnabled(*i, request->enabled());
	}
	SyncVdpBreakpoints();
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::del_breakpoint(ServerContext*, const DbgBreakpoint* request, Empty*)
{
	std::unique_lock<std::mutex> lock(_breakpointMutex);
	auto i = FindBreakpoint(*request);
	if (i != _breakpoints.end())
	{
		DeleteExodusBreakpoint(*i);
		_breakpoints.erase(i);
	}
	SyncVdpBreakpoints();
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::clear_breakpoints(ServerContext*, const Empty*, Empty*)
{
	std::unique_lock<std::mutex> lock(_breakpointMutex);
	for (auto& entry : _breakpoints)
	{
		DeleteExodusBreakpoint(entry);
	}
	_breakpoints.clear();
	SyncVdpBreakpoints();
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
// Execution functions
// _running is only touched under _executeMutex so a quick stop can't slip past the monitor
//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::pause(ServerContext*, const Empty*, Empty*)
{
	std::unique_lock<std::mutex> lock(_executeMutex);
	if (_system.SystemRunning())
	{
		_stopRequested = true;
		_system.StopSystem();
		_running = true;
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::resume(ServerContext*, const Empty*, Empty*)
{
	std::unique_lock<std::mutex> lock(_executeMutex);
	_stopRequested = false;
	_system.RunSystem();
	_running = true;
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::step_into(ServerContext*, const Empty*, Empty*)
{
	IDevice* device = GetM68000Device();
	if (device == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	std::unique_lock<std::mutex> lock(_executeMutex);
	_stopRequested = true;
	_system.ExecuteDeviceStep(device);
	_running = true;
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::step_over(ServerContext*, const Empty*, Empty*)
{
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	std::unique_lock<std::mutex> lock(_executeMutex);
	_stopRequested = true;
	cpu->BreakOnStepOverCurrentOpcode();
	_system.RunSystem();
	_running = true;
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::get_callstack(ServerContext*, const Empty*, Callstack* response)
{
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return Status(grpc::StatusCode::UNAVAILABLE, "no 68000");
	}

	// Exodus keeps the newest call first, IDA wants the oldest first
	std::list<IProcessor::CallStackEntry> callStack = cpu->GetCallStack();
	for (auto i = callStack.rbegin(); i != callStack.rend(); ++i)
	{
		response->add_callstack(i->sourceAddress & 0xFFFFFF);
	}
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::start_emulation(ServerContext*, const Empty*, Empty*)
{
	{
		std::unique_lock<std::mutex> lock(_clientMutex);
		auto channel = grpc::CreateChannel("localhost:" + std::to_string(_eventPort), grpc::InsecureChannelCredentials());
		if (!channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(5)))
		{
			return Status(grpc::StatusCode::UNAVAILABLE, "can't reach IDA");
		}
		_client = DbgClient::NewStub(channel);

		grpc::ClientContext context;
		Empty request;
		Empty reply;
		_client->start_event(&context, request, &reply);
	}

	StartTrace();

	// IDA expects the target to come up paused
	std::unique_lock<std::mutex> lock(_executeMutex);
	_stopRequested = true;
	_system.StopSystem();
	_running = true;
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
Status DbgServerImpl::exit_emulation(ServerContext*, const Empty*, Empty*)
{
	DrainTrace();
	SendStopEvent();
	StopTrace();

	std::unique_lock<std::mutex> lock(_clientMutex);
	if (!_client)
	{
		return Status::CANCELLED;
	}
	_client.reset();
	return Status::OK;
}

//----------------------------------------------------------------------------------------------------------------------
// Event functions
//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::MonitorThread()
{
	unsigned int pollCount = 0;
	while (!_stopMonitor)
	{
		bool stopped = false;
		{
			std::unique_lock<std::mutex> lock(_executeMutex);
			if (_system.SystemRunning())
			{
				_running = true;
			}
			else
			{
				stopped = _running.exchange(false);
			}
		}

		// Draining costs a full copy of the trace buffer, so not on every poll
		if (stopped || ((++pollCount % 4) == 0))
		{
			DrainTrace();
		}

		if (stopped)
		{
			if (ShouldBreak())
			{
				_stopRequested = false;
				SendPauseEvent();
			}
			else
			{
				std::unique_lock<std::mutex> lock(_executeMutex);
				_system.RunSystem();
				_running = true;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

//----------------------------------------------------------------------------------------------------------------------
bool DbgServerImpl::ShouldBreak()
{
	if (_stopRequested)
	{
		return true;
	}
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return true;
	}
	unsigned int pc = cpu->GetCurrentPC() & 0xFFFFFF;

	// Exodus doesn't say which watchpoint hit, so if no PC breakpoint matches try them all
	std::list<DbgBreakpoint> candidates;
	{
		std::unique_lock<std::mutex> lock(_breakpointMutex);
		for (const auto& entry : _breakpoints)
		{
			if (entry.info.enabled() && (entry.info.type() == BP_PC) && !entry.info.is_vdp() && (pc >= entry.info.bstart()) && (pc <= entry.info.bend()))
			{
				candidates.push_back(entry.info);
			}
		}
		if (candidates.empty())
		{
			for (const auto& entry : _breakpoints)
			{
				if (entry.info.enabled() && (entry.info.type() != BP_PC))
				{
					candidates.push_back(entry.info);
				}
			}
		}
	}

	// Nothing of ours matched, so it was stopped from the Exodus side
	if (candidates.empty())
	{
		return true;
	}
	for (const auto& info : candidates)
	{
		if (info.condition().empty() || EvalCondition(info))
		{
			return true;
		}
	}
	return false;
}

//----------------------------------------------------------------------------------------------------------------------
bool DbgServerImpl::EvalCondition(const DbgBreakpoint& info)
{
	std::unique_lock<std::mutex> lock(_clientMutex);
	if (!_client)
	{
		return true;
	}

	Condition request;
	request.set_elang(info.elang());
	request.set_condition(info.condition());
	google::protobuf::BoolValue reply;
	grpc::ClientContext context;
	if (!_client->eval_condition(&context, request, &reply).ok())
	{
		return true;
	}
	return reply.value();
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::SendPauseEvent()
{
	IProcessor* cpu = GetProcessor();
	if (cpu == 0)
	{
		return;
	}

	PauseChanged request;
	request.set_address(cpu->GetCurrentPC() & 0xFFFFFF);
	TakeChanged(*request.mutable_changed());

	std::unique_lock<std::mutex> lock(_clientMutex);
	if (!_client)
	{
		return;
	}
	grpc::ClientContext context;
	Empty reply;
	_client->pause_event(&context, request, &reply);
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::SendStopEvent()
{
	Changed request;
	TakeChanged(*request.mutable_changed());

	std::unique_lock<std::mutex> lock(_clientMutex);
	if (!_client)
	{
		return;
	}
	grpc::ClientContext context;
	Empty reply;
	_client->stop_event(&context, request, &reply);
}

//----------------------------------------------------------------------------------------------------------------------
// Trace functions
// IDA gets a map of executed address to previous address and turns those bytes into code
//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::StartTrace()
{
	IProcessor* cpu = GetProcessor();
	std::unique_lock<std::mutex> lock(_traceMutex);
	if ((cpu == 0) || _tracing)
	{
		return;
	}

	_savedTraceEnabled = cpu->GetTraceEnabled();
	_savedTraceDisassemble = cpu->GetTraceDisassemble();
	_savedTraceLength = cpu->GetTraceLength();
	cpu->SetTraceDisassemble(false);
	cpu->SetTraceLength(TraceLength);
	cpu->ClearTraceLog();
	cpu->SetTraceEnabled(true);

	_tracing = true;
	_lastTracePc = 0;
	_changed.clear();
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::StopTrace()
{
	IProcessor* cpu = GetProcessor();
	std::unique_lock<std::mutex> lock(_traceMutex);
	if (!_tracing)
	{
		return;
	}
	_tracing = false;
	_changed.clear();
	if (cpu == 0)
	{
		return;
	}

	cpu->SetTraceEnabled(_savedTraceEnabled);
	cpu->SetTraceLength(_savedTraceLength);
	cpu->SetTraceDisassemble(_savedTraceDisassemble);
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::DrainTrace()
{
	IProcessor* cpu = GetProcessor();
	std::unique_lock<std::mutex> lock(_traceMutex);
	if ((cpu == 0) || !_tracing)
	{
		return;
	}

	std::vector<IProcessor::TraceLogEntry> log = cpu->GetTraceLog();
	cpu->ClearTraceLog();
	for (const auto& entry : log)
	{
		unsigned int pc = entry.address & 0xFFFFFF;
		if ((_lastTracePc != 0) && (pc != 0) && (pc < MaxRomSize))
		{
			_changed[pc] = _lastTracePc;
		}
		_lastTracePc = pc;
	}
}

//----------------------------------------------------------------------------------------------------------------------
void DbgServerImpl::TakeChanged(google::protobuf::Map<uint32_t, uint32_t>& changed)
{
	std::unique_lock<std::mutex> lock(_traceMutex);
	for (const auto& i : _changed)
	{
		changed[i.first] = i.second;
	}
	_changed.clear();
}

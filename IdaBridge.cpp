#include "DbgServer.h"
#include "IdaBridge.h"

//----------------------------------------------------------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------------------------------------------------------
IdaBridge::IdaBridge(const std::wstring& implementationName, const std::wstring& instanceName, unsigned int moduleID)
:Extension(implementationName, instanceName, moduleID)
{ }

//----------------------------------------------------------------------------------------------------------------------
IdaBridge::~IdaBridge()
{
	StopServer();
}

//----------------------------------------------------------------------------------------------------------------------
// Initialization functions
//----------------------------------------------------------------------------------------------------------------------
bool IdaBridge::BindToSystemInterface(ISystemExtensionInterface* systemInterface)
{
	if (!Extension::BindToSystemInterface(systemInterface))
	{
		return false;
	}
	StartServer();
	return true;
}

//----------------------------------------------------------------------------------------------------------------------
// Server functions
//----------------------------------------------------------------------------------------------------------------------
void IdaBridge::StartServer()
{
	if (_server)
	{
		return;
	}

	// IDA listens for events 1000 ports up, same as with Gens
	_service.reset(new DbgServerImpl(GetSystemInterface(), DefaultPort + 1000));

	grpc::ServerBuilder builder;
	builder.AddListeningPort("127.0.0.1:" + std::to_string(DefaultPort), grpc::InsecureServerCredentials());
	builder.RegisterService(_service.get());
	_server = builder.BuildAndStart();
	if (!_server)
	{
		_service.reset();
		return;
	}

	_serverThread = std::thread([this]() { _server->Wait(); });
}

//----------------------------------------------------------------------------------------------------------------------
void IdaBridge::StopServer()
{
	if (!_server)
	{
		return;
	}

	_server->Shutdown(std::chrono::system_clock::now() + std::chrono::milliseconds(100));
	if (_serverThread.joinable())
	{
		_serverThread.join();
	}
	_server.reset();
	_service.reset();
}

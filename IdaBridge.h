#ifndef __IDABRIDGE_H__
#define __IDABRIDGE_H__
#include "Extension/Extension.pkg"
#include <memory>
#include <thread>
class DbgServerImpl;
namespace grpc { class Server; }

class IdaBridge :public Extension
{
public:
	// Constructors
	IdaBridge(const std::wstring& implementationName, const std::wstring& instanceName, unsigned int moduleID);
	~IdaBridge();

	// Initialization functions
	virtual bool BindToSystemInterface(ISystemExtensionInterface* systemInterface);

private:
	void StartServer();
	void StopServer();

private:
	static const int DefaultPort = 23946;

	std::unique_ptr<DbgServerImpl> _service;
	std::unique_ptr<grpc::Server> _server;
	std::thread _serverThread;
};

#endif

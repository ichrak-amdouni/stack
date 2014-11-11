/*
 * Flow Allocator
 *
 *    Bernat Gaston <bernat.gaston@i2cat.net>
 *    Eduard Grasa <eduard.grasa@i2cat.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef IPCP_FLOW_ALLOCATOR_HH
#define IPCP_FLOW_ALLOCATOR_HH

#ifdef __cplusplus

#include <librina/timer.h>

#include "common/concurrency.h"
#include "ipcp/components.h"

namespace rinad {

/// Flow Allocator Instance Interface
class IFlowAllocatorInstance {
public:
	virtual ~IFlowAllocatorInstance() {
	}
	;

	/// Returns the portId associated to this Flow Allocator Instance
	/// @return
	virtual int get_port_id() const = 0;

	/// Return the Flow object associated to this Flow Allocator Instance
	/// @return
	virtual Flow * get_flow() const = 0;

	/// True if FAI is no longer operative, false otherwise
	virtual bool isFinished() const = 0;

	/// Generate the flow object, create the local DTP and optionally DTCP instances, generate a CDAP
	/// M_CREATE request with the flow object and send it to the appropriate IPC process (search the
	/// directory and the directory forwarding table if needed)
	/// @param flowRequestEvent The flow allocation request
	/// @throws Exception if there are not enough resources to fulfill the allocate request
	virtual void submitAllocateRequest(const rina::FlowRequestEvent& event) = 0;

	virtual void processCreateConnectionResponseEvent(
			const rina::CreateConnectionResponseEvent& event) = 0;

	/// When an FAI is created with a Create_Request(Flow) as input, it will inspect the parameters
	/// first to determine if the requesting Application (Source_Naming_Info) has access to the requested
	/// Application (Destination_Naming_Info) by inspecting the Access Control parameter.  If not, a
	/// negative Create_Response primitive will be returned to the requesting FAI. If it does have access,
	/// the FAI will determine if the policies proposed are acceptable, invoking the NewFlowRequestPolicy.
	/// If not, a negative Create_Response is sent.  If they are acceptable, the FAI will invoke a
	/// Allocate_Request.deliver primitive to notify the requested Application that it has an outstanding
	/// allocation request.  (If the application is not executing, the FAI will cause the application
	/// to be instantiated.)
	/// @param flow
	/// @param portId the destination portid as decided by the Flow allocator
	/// @param requestMessate the CDAP request message
	/// @param underlyingPortId the port id to reply later on
	virtual void createFlowRequestMessageReceived(Flow * flow, const std::string& object_name,
			int invoke_id, int underlyingPortId) = 0;

	/// When the FAI gets a Allocate_Response from the destination application,
	/// it formulates a Create_Response on the flow object requested.If the
	/// response was positive, the FAI will cause DTP and if required DTCP instances
	/// to be created to support this allocation. A positive Create_Response Flow is
	/// sent to the requesting FAI with the connection-endpoint-id and other information
	/// provided by the destination FAI. The Create_Response is sent to requesting FAI
	/// with the necessary information reflecting the existing flow, or an indication as
	/// to why the flow was refused. If the response was negative, the FAI does any
	/// necessary housekeeping and terminates.
	/// @param AllocateFlowResponseEvent - the reply from the application
	/// @throws IPCException
	virtual void submitAllocateResponse(
			const rina::AllocateFlowResponseEvent& event) = 0;

	virtual void processCreateConnectionResultEvent(
			const rina::CreateConnectionResultEvent& event) = 0;

	virtual void processUpdateConnectionResponseEvent(
			const rina::UpdateConnectionResponseEvent& event) = 0;

	/// When a deallocate primitive is invoked, it is passed to the FAI responsible
	/// for that port-id. The FAI sends an M_DELETE request CDAP PDU on the Flow
	/// object referencing the destination port-id, deletes the local binding between
	/// the Application and the DTP-instance and waits for a response.  (Note that
	/// the DTP and DTCP if it exists will be deleted automatically after 2MPL)
	/// @param the flow deallocate request event
	/// @throws IPCException
	virtual void submitDeallocate(
			const rina::FlowDeallocateRequestEvent& event) = 0;

	/// When this PDU is received by the FAI with this port-id, the FAI invokes
	/// a Deallocate.deliver to notify the local Application,
	/// deletes the binding between the Application and the local DTP-instance,
	/// and sends a Delete_Response indicating the result.
	virtual void deleteFlowRequestMessageReceived() = 0;

	virtual void destroyFlowAllocatorInstance(const std::string& flowObjectName,
			bool requestor) = 0;

	virtual unsigned int get_allocate_response_message_handle() const = 0;
	virtual void set_allocate_response_message_handle(
			unsigned int allocate_response_message_handle) = 0;
};

/// Representation of a flow object in the RIB
class FlowRIBObject: public SimpleSetMemberIPCPRIBObject {
public:
	FlowRIBObject(IPCProcess * ipc_process, const std::string& object_name,
			const std::string& object_class,
			IFlowAllocatorInstance * flow_allocator_instance);
	void remoteDeleteObject(int invoke_id, rina::CDAPSessionDescriptor * session_descriptor);
	std::string get_displayable_value();

private:
	IFlowAllocatorInstance * flow_allocator_instance_;
};

/// Representation of a set of Flow objects in the RIB
class FlowSetRIBObject: public BaseIPCPRIBObject {
public:
	FlowSetRIBObject(IPCProcess * ipc_process, IFlowAllocator * flow_allocator);
	void remoteCreateObject(void * object_value, const std::string& object_name,
			int invoke_id, rina::CDAPSessionDescriptor * session_descriptor);
	using rina::BaseRIBObject::createObject;
	void createObject(const std::string& objectClass,
			const std::string& objectName, const void* objectValue);
	const void* get_value() const;

private:
	IFlowAllocator * flow_allocator_;
};

class QoSCubeRIBObject: public SimpleSetMemberIPCPRIBObject {
public:
	QoSCubeRIBObject(IPCProcess* ipc_process,
			const std::string& object_class,
			const std::string& object_name,
			const rina::QoSCube* cube);
	std::string get_displayable_value();
};

/// Representation of a set of QoS cubes in the RIB
class QoSCubeSetRIBObject: public BaseIPCPRIBObject {
public:
	QoSCubeSetRIBObject(IPCProcess * ipc_process);
	void remoteCreateObject(void * object_value, const std::string& object_name,
			int invoke_id, rina::CDAPSessionDescriptor * session_descriptor);
	using rina::BaseRIBObject::createObject;
	void createObject(const std::string& objectClass,
			const std::string& objectName, const void* objectValue);
	void deleteObject(const void* objectValue);
	const void* get_value() const;
};

/// Implementation of the Flow Allocator component
class FlowAllocator: public IFlowAllocator {
public:
	FlowAllocator();
	~FlowAllocator();
	void set_ipc_process(IPCProcess * ipc_process);
	void set_dif_configuration(const rina::DIFConfiguration& dif_configuration);
	void createFlowRequestMessageReceived(Flow * flow, const std::string& object_name,
			int invoke_id, int underlying_port_id);
	void submitAllocateRequest(rina::FlowRequestEvent& flowRequestEvent);
	void processCreateConnectionResponseEvent(
			const rina::CreateConnectionResponseEvent& event);
	void submitAllocateResponse(const rina::AllocateFlowResponseEvent& event);
	void processCreateConnectionResultEvent(
			const rina::CreateConnectionResultEvent& event);
	void processUpdateConnectionResponseEvent(
			const rina::UpdateConnectionResponseEvent& event);
	void submitDeallocate(const rina::FlowDeallocateRequestEvent& event);
	void removeFlowAllocatorInstance(int portId);

private:
	/// Flow allocator instances, each one associated to a port-id
	rina::ThreadSafeMapOfPointers<int, IFlowAllocatorInstance> flow_allocator_instances_;

	IPCProcess * ipc_process_;
	IPCPRIBDaemon * rib_daemon_;
	rina::CDAPSessionManagerInterface * cdap_session_manager_;
	Encoder * encoder_;
	INamespaceManager * namespace_manager_;

	/// Create initial RIB objects
	void populateRIB();

	/// Reply to the IPC Manager
	void replyToIPCManager(const rina::FlowRequestEvent& event, int result);
};

/// This policy is used to convert an Allocate Request is into a create_flow request.
/// Its primary task is to translate the request into the proper QoS-class-set, flow set,
/// and access control capabilities.
class INewFlowRequetPolicy {
public:
	virtual ~INewFlowRequetPolicy() {
	}
	;
	virtual Flow * generateFlowObject(IPCProcess * ipc_process,
			const rina::FlowRequestEvent& flowRequestEvent) = 0;
};

class SimpleNewFlowRequestPolicy: public INewFlowRequetPolicy {
public:
	SimpleNewFlowRequestPolicy() {
	}
	;
	~SimpleNewFlowRequestPolicy() {
	}
	;
	Flow * generateFlowObject(IPCProcess * ipc_process,
			const rina::FlowRequestEvent& flowRequestEvent);

private:
	rina::QoSCube * selectQoSCube(IPCProcess * ipc_process,
			const rina::FlowSpecification& flowSpec);
	std::list<rina::QoSCube*> getQoSCubes(IPCProcess * ipc_process);
};

///Implementation of the FlowAllocatorInstance
class FlowAllocatorInstance: public IFlowAllocatorInstance,
		public rina::BaseCDAPResponseMessageHandler {
public:
	enum FAIState {
		NO_STATE,
		CONNECTION_CREATE_REQUESTED,
		MESSAGE_TO_PEER_FAI_SENT,
		APP_NOTIFIED_OF_INCOMING_FLOW,
		CONNECTION_UPDATE_REQUESTED,
		FLOW_ALLOCATED,
		CONNECTION_DESTROY_REQUESTED,
		WAITING_2_MPL_BEFORE_TEARING_DOWN,
		FINISHED
	};

	FlowAllocatorInstance(IPCProcess * ipc_process,
			IFlowAllocator * flow_allocator,
			rina::CDAPSessionManagerInterface * cdap_session_manager,
			int port_id);
	FlowAllocatorInstance(IPCProcess * ipc_process,
			IFlowAllocator * flow_allocator, int port_id);
	~FlowAllocatorInstance();
	int get_port_id() const;
	Flow * get_flow() const;
	bool isFinished() const;
	unsigned int get_allocate_response_message_handle() const;
	void set_allocate_response_message_handle(
			unsigned int allocate_response_message_handle);
	void submitAllocateRequest(const rina::FlowRequestEvent& event);
	void processCreateConnectionResponseEvent(
			const rina::CreateConnectionResponseEvent& event);
	void createFlowRequestMessageReceived(Flow * flow, const std::string& object_name,
			int invoke_id, int underlyingPortId);
	void processCreateConnectionResultEvent(
			const rina::CreateConnectionResultEvent& event);
	void submitAllocateResponse(const rina::AllocateFlowResponseEvent& event);
	void processUpdateConnectionResponseEvent(
			const rina::UpdateConnectionResponseEvent& event);
	void submitDeallocate(const rina::FlowDeallocateRequestEvent& event);
	void deleteFlowRequestMessageReceived();
	void destroyFlowAllocatorInstance(const std::string& flowObjectName,
			bool requestor);

	/// If the response to the allocate request is negative
	/// the Allocation invokes the AllocateRetryPolicy. If the AllocateRetryPolicy returns a
	/// positive result, a new Create_Flow Request is sent and the CreateFlowTimer is reset.
	/// Otherwise, if the AllocateRetryPolicy returns a negative result or the MaxCreateRetries
	/// has been exceeded, an Allocate_Request.deliver primitive to notify the Application that
	/// the flow could not be created. (If the reason was "Application Not Found", the primitive
	/// will be delivered to the Inter-DIF Directory to search elsewhere.The FAI deletes the DTP
	/// and DTCP instances it created and does any other housekeeping necessary, before
	/// terminating.  If the response is positive, it completes the binding of the DTP-instance
	/// with this connection-endpoint-id to the requesting Application and invokes a
	/// Allocate_Request.submit primitive to notify the requesting Application that its allocation
	/// request has been satisfied.
	void createResponse(int result, const std::string& result_reason,
			void * object_value, rina::CDAPSessionDescriptor * session_descriptor);

private:
	IPCProcess * ipc_process_;
	IFlowAllocator * flow_allocator_;
	rina::CDAPSessionManagerInterface * cdap_session_manager_;
	Encoder * encoder_;
	IPCPRIBDaemon * rib_daemon_;
	INamespaceManager * namespace_manager_;
	SecurityManager * security_manager_;
	INewFlowRequetPolicy * new_flow_request_policy_;
	FAIState state;

	rina::Timer timer;

	/// The portId associated to this Flow Allocator instance
	int port_id_;

	/// The flow object related to this Flow Allocator Instance
	Flow * flow_;

	/// The event requesting the allocation of the flow
	rina::FlowRequestEvent flow_request_event_;

	/// The name of the flow object associated to this FlowAllocatorInstance
	std::string object_name_;

	unsigned int allocate_response_message_handle_;
	int invoke_id_;
	int underlying_port_id_;
	rina::Lockable * lock_;
	rina::Timer * timer_;

	void initialize(IPCProcess * ipc_process, IFlowAllocator * flow_allocator,
			int port_id);
	void replyToIPCManager(rina::FlowRequestEvent & event, int result);
	void releasePortId();

	/// Release the port-id, unlock and remove the FAI from the FA
	void releaseUnlockRemove();
};

class TearDownFlowTimerTask: public rina::TimerTask {
public:
	static const long DELAY;

	TearDownFlowTimerTask(FlowAllocatorInstance * flow_allocator_instance,
			const std::string& flow_object_name, bool requestor);
	~TearDownFlowTimerTask() throw () {
	}

	void run();

private:
	FlowAllocatorInstance * flow_allocator_instance_;
	std::string flow_object_name_;
	bool requestor_;
};

class DataTransferConstantsRIBObject: public BaseIPCPRIBObject {
public:
	DataTransferConstantsRIBObject(IPCProcess * ipc_process);
	void remoteReadObject(int invoke_id,
			rina::CDAPSessionDescriptor * cdapSessionDescriptor);
	void remoteCreateObject(void * object_value, const std::string& object_name,
			int invoke_id, rina::CDAPSessionDescriptor * session_descriptor);
	void createObject(const std::string& objectClass,
			const std::string& objectName,
			const void* objectValue);
	void writeObject(const void* object_value);
	const void* get_value() const;
	std::string get_displayable_value();

private:
	rina::CDAPSessionManagerInterface * cdap_session_manager_;
};

/// Encoder of the Flow
class FlowEncoder: public rina::EncoderInterface {
public:
	const rina::SerializedObject* encode(const void* object);
	void* decode(const rina::ObjectValueInterface * object_value) const;
};

}

#endif

#endif

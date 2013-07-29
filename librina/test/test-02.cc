//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <iostream>
#include "librina.h"
#include "core.h"

using namespace rina;

bool checkIPCProcesses(unsigned int expectedProcesses) {
	std::vector<IPCProcess *> ipcProcesses = ipcProcessFactory
			->listIPCProcesses();
	if (ipcProcesses.size() != expectedProcesses) {
		std::cout << "ERROR: Expected " << expectedProcesses
				<< " IPC Processes, but only found " + ipcProcesses.size()
				<< "\n";
		return false;
	}

	std::cout << "Ids of existing processes:";
	for (unsigned int i = 0; i < ipcProcesses.size(); i++) {
		std::cout << " " << ipcProcesses.at(i)->getId() << ",";
	}
	std::cout << "\n";

	return true;
}

bool checkRecognizedEvent(IPCEvent * event) {
	switch (event->getType()) {
	case APPLICATION_REGISTRATION_REQUEST_EVENT: {
		ApplicationRegistrationRequestEvent * appREvent =
				dynamic_cast<ApplicationRegistrationRequestEvent *>(event);
		std::cout << "Got application registration request from application "
				<< appREvent->getApplicationName().getProcessName() << "\n";
		break;
	}
	case APPLICATION_UNREGISTRATION_REQUEST_EVENT: {
		ApplicationUnregistrationRequestEvent * appUEvent =
				dynamic_cast<ApplicationUnregistrationRequestEvent *>(event);
		std::cout
				<< "Got application unregistration request from application "
				<< appUEvent->getApplicationName().getProcessName() << "\n";
		break;
	}
	default:
		std::cout << "Unrecognized event type\n";
		return false;
	}

	return true;
}

int main(int argc, char * argv[]) {
	std::cout << "TESTING LIBRINA-IPCMANAGER\n";

	/* TEST CREATE IPC PROCESS */
	ApplicationProcessNamingInformation * ipcProcessName1 =
			new ApplicationProcessNamingInformation(
					"/ipcprocess/i2CAT/Barcelona", "1");
	ApplicationProcessNamingInformation * ipcProcessName2 =
			new ApplicationProcessNamingInformation(
					"/ipcprocess/i2CAT/Barcelona/shim", "1");
	ApplicationProcessNamingInformation * sourceName =
			new ApplicationProcessNamingInformation("/apps/test/source", "1");
	ApplicationProcessNamingInformation * destinationName =
			new ApplicationProcessNamingInformation("/apps/test/destination",
					"1");
	ApplicationProcessNamingInformation * difName =
			new ApplicationProcessNamingInformation("/difs/Test.DIF", "");

	IPCProcess * ipcProcess1 = ipcProcessFactory->create(*ipcProcessName1,
			DIF_TYPE_NORMAL);
	IPCProcess * ipcProcess2 = ipcProcessFactory->create(*ipcProcessName2,
			DIF_TYPE_SHIM_ETHERNET);

	/* TEST LIST IPC PROCESSES */
	if (!checkIPCProcesses(2)) {
		return 1;
	}

	/* TEST DESTROY */
	ipcProcessFactory->destroy(ipcProcess2->getId());
	if (!checkIPCProcesses(1)) {
		return 1;
	}

	/* TEST ASSIGN TO DIF */
	DIFConfiguration * difConfiguration = new DIFConfiguration();
	ipcProcess1->assignToDIF(*difConfiguration);

	/* TEST REGISTER APPLICATION */
	ipcProcess1->registerApplication(*sourceName, 37);

	/* TEST UNREGISTER APPLICATION */
	ipcProcess1->unregisterApplication(*sourceName);

	/* TEST ALLOCATE FLOW */
	FlowSpecification *flowSpec = new FlowSpecification();
	FlowRequestEvent * flowRequest = new FlowRequestEvent(*flowSpec,
			*sourceName, *difName, 1234);
	flowRequest->setPortId(430);
	ipcProcess1->allocateFlow(*flowRequest, 24);

	/* TEST QUERY RIB */
	ipcProcess1->queryRIB("list of flows",
			"/dif/management/flows/", 0, 0, "");

	/* TEST APPLICATION REGISTERED */
	ApplicationRegistrationRequestEvent * event = new
			ApplicationRegistrationRequestEvent(*sourceName, *difName, 34);
	applicationManager->applicationRegistered(*event, 0,
			0,0,"Everything was fine");

	/* TEST APPLICATION UNREGISTERED */
	ApplicationUnregistrationRequestEvent * event2 = new
			ApplicationUnregistrationRequestEvent(*sourceName, *difName, 34);
	applicationManager->applicationUnregistered(*event2, 0, "ok");

	/* TEST FLOW ALLOCATED */
	FlowRequestEvent * flowEvent = new FlowRequestEvent(25, *flowSpec,
			*sourceName, *destinationName, *difName, 3);
	applicationManager->flowAllocated(*flowEvent, "ok", 1, 1);

	ipcProcessFactory->destroy(ipcProcess1->getId());
	if (!checkIPCProcesses(0)) {
		return 1;
	}

	delete ipcProcessName1;
	delete ipcProcessName2;
	delete sourceName;
	delete destinationName;
	delete difName;
	delete difConfiguration;
	delete flowSpec;
	delete flowRequest;
	return 0;
}
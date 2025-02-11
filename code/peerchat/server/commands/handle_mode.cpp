#include <OS/OpenSpy.h>

#include <OS/Buffer.h>
#include <OS/KVReader.h>
#include <sstream>
#include <algorithm>

#include <OS/gamespy/gamespy.h>
#include <OS/SharedTasks/tasks.h>
#include <tasks/tasks.h>


#include <server/Driver.h>
#include <server/Server.h>
#include <server/Peer.h>
namespace Peerchat {
	void Peer::OnMode_UpdateUserMode(TaskResponse response_data, Peer* peer) {
		if (response_data.error_details.response_code == TaskShared::WebErrorCode_Success) {
			peer->m_user_details.modeflags = response_data.summary.modeflags;
		}
		else if (response_data.error_details.response_code == TaskShared::WebErrorCode_NoSuchUser) {
			peer->send_no_such_target_error(response_data.profile.uniquenick);
		}
	}
	void Peer::OnMode_FetchChannelInfo(TaskResponse response_data, Peer* peer) {
		std::ostringstream s;
		if (response_data.error_details.response_code == TaskShared::WebErrorCode_Success) {
			s << response_data.channel_summary.channel_name << " ";
			s << "+";
			for (int i = 0; i < num_channel_mode_flags; i++) {
				if (response_data.channel_summary.basic_mode_flags & channel_mode_flag_map[i].flag) {
					s << channel_mode_flag_map[i].character;
				}
			}
			if(response_data.channel_summary.password.length() > 0) {
				s <<  "k";
			}

			if(response_data.channel_summary.limit > 0) {
				s <<  "l";
			}

			if(response_data.channel_summary.password.length() > 0) {
				s << " " << response_data.channel_summary.password;
			}

			if(response_data.channel_summary.limit > 0) {
				s << " " << response_data.channel_summary.limit;
			}
			peer->send_numeric(324, s.str(), true);

			s.str("");
			s << response_data.channel_summary.channel_name << " " << response_data.channel_summary.created_at.tv_sec;
			peer->send_numeric(329, s.str(), true);
		}
		else if (response_data.error_details.response_code == TaskShared::WebErrorCode_NoSuchUser) {
			peer->send_no_such_target_error(response_data.channel_summary.channel_name);
		}
	}
	void Peer::OnMode_FetchUserInfo(TaskResponse response_data, Peer* peer) {
		std::ostringstream s;
		if (response_data.error_details.response_code == TaskShared::WebErrorCode_Success) {
			s << "+";
			for (int i = 0; i < num_user_mode_flags; i++) {
				if (response_data.summary.modeflags & user_mode_flag_map[i].flag) {
					s << user_mode_flag_map[i].character;
				}
			}
			peer->send_numeric(221, s.str(), true);
		}
		else if (response_data.error_details.response_code == TaskShared::WebErrorCode_NoSuchUser) {
			peer->send_no_such_target_error(response_data.channel_summary.channel_name);
		}
	}
	void Peer::OnMode_FetchBanInfo(TaskResponse response_data, Peer* peer) {
		std::ostringstream ss;
		if (response_data.error_details.response_code == TaskShared::WebErrorCode_Success) {
			if (response_data.usermode.usermodeid != 0 && response_data.usermode.modeflags & EUserChannelFlag_Banned && response_data.usermode.hostmask.length() > 0) {
				ss << "*@" << response_data.usermode.hostmask << " " << response_data.usermode.setByUserSummary.nick << " " << response_data.usermode.set_at.tv_sec;
				peer->send_numeric(367, ss.str(), true, response_data.channel_summary.channel_name);
			}
			if (response_data.is_end) {
				peer->send_numeric(368, "End of Channel Ban List", false, response_data.channel_summary.channel_name);
			}
		}
		else if (response_data.error_details.response_code == TaskShared::WebErrorCode_NoSuchUser) {
			peer->send_no_such_target_error(response_data.channel_summary.channel_name);
		}
	}
	void Peer::handle_ban_hostmask(std::string channel, std::string hostmask, bool set) {
		TaskScheduler<PeerchatBackendRequest, TaskThreadData>* scheduler = ((Peerchat::Server*)(GetDriver()->getServer()))->GetPeerchatTask();

		PeerchatBackendRequest req;
		req.channel_summary.channel_name = channel;
		req.type = EPeerchatRequestType_UpdateChannelModes_BanMask;

		req.peer = this;
		req.peer->IncRef();
		
		if(set) {
			req.channel_modify.set_usermodes[hostmask] |= EUserChannelFlag_Banned;
		} else {
			req.channel_modify.unset_usermodes[hostmask] |= EUserChannelFlag_Banned;
		}

		scheduler->AddRequest(req.type, req);
	}
	void Peer::handle_channel_mode_command(std::vector<std::string> data_parser) {
		TaskScheduler<PeerchatBackendRequest, TaskThreadData>* scheduler = ((Peerchat::Server*)(GetDriver()->getServer()))->GetPeerchatTask();
		PeerchatBackendRequest req;
		bool includeBanLookup = false;

		req.channel_summary.channel_name = data_parser.at(1);

		if (data_parser.size() == 2) {
			//lookup
			req.type = EPeerchatRequestType_LookupChannelDetails;
			req.peer = this;
			req.channel_summary.channel_name = data_parser.at(1);
			req.peer->IncRef();
			req.callback = OnMode_FetchChannelInfo;
			scheduler->AddRequest(req.type, req);
		}
		else if (data_parser.size() >= 3) { //set/unset
			// +m-n
			size_t last_offset = 2;

			bool set = true;
			int set_flags = 0, unset_flags = 0;
			std::string mode_string = data_parser.at(2);
			for (size_t i = 0; i < mode_string.length(); i++) {
				if (mode_string[i] == '+') {
					set = true;
				}
				else if (mode_string[i] == '-') {
					set = false;
				}
				else if (mode_string[i] == 'b') {
					last_offset++;
					if (data_parser.size()-1 < last_offset) {
						includeBanLookup = true;
					}
					else {						
						//make seperate set usermode call if set... otherwise try unset
						if (data_parser.size()-1 < last_offset) {
							continue;
						}
						std::string ban_mask = data_parser.at(last_offset);
						handle_ban_hostmask(req.channel_summary.channel_name, ban_mask, set);
					}
				}
				else if (mode_string[i] == 'k') {
					req.channel_modify.update_password = true;
					if (set) {
						last_offset++;
						if (data_parser.size()-1 < last_offset) {
							continue;
						}
						req.channel_modify.password = data_parser.at(last_offset);
					}
					else {
						req.channel_modify.password = "";
					}
				}
				else if (mode_string[i] == 'l') {
					req.channel_modify.update_limit = true;
					if (set) {
						last_offset++;
						if (data_parser.size()-1 < last_offset) {
							continue;
						}
						req.channel_modify.limit = atoi(data_parser.at(last_offset).c_str());
					}
					else {
						req.channel_modify.limit = 0;
					}
				}
				else if (mode_string[i] == 'v') {
					last_offset++;
					if (data_parser.size()-1 < last_offset) {
						continue;
					}
					if (set) {
						req.channel_modify.set_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_Voice;
					}
					else {
						req.channel_modify.unset_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_Voice;
					}
				}
				else if (mode_string[i] == 'h') {
					last_offset++;
					if (data_parser.size()-1 < last_offset) {
						continue;
					}
					if (set) {
						req.channel_modify.set_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_HalfOp;
					}
					else {
						req.channel_modify.unset_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_HalfOp;
					}
				}
				else if (mode_string[i] == 'o') {
					last_offset++;
					if (data_parser.size()-1 < last_offset) {
						continue;
					}
					if (set) {
						req.channel_modify.set_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_Op;
					}
					else {
						req.channel_modify.unset_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_Op;
					}
				}
				else if (mode_string[i] == 'O') {
					last_offset++;
					if (data_parser.size()-1 < last_offset) {
						continue;
					}
					if (set) {
						req.channel_modify.set_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_Owner;
					}
					else {
						req.channel_modify.unset_usermodes[data_parser.at(last_offset)] |= EUserChannelFlag_Owner;
					}
				}
				else {
					ModeFlagMap flag;
					bool found = false;
					for (int x = 0; x < num_channel_mode_flags; x++) {
						if (channel_mode_flag_map[x].character == mode_string[i]) {
							flag = channel_mode_flag_map[x];
							found = true;
							break;
						}
					}
					if (found) {
						if (set) {
							set_flags |= flag.flag;
							unset_flags &= ~flag.flag;
						}
						else {
							unset_flags |= flag.flag;
							set_flags &= ~flag.flag;
						}
					}
				}
			}
			req.type = EPeerchatRequestType_UpdateChannelModes;
			req.peer = this;
			req.summary = m_user_details;
			req.channel_modify.set_mode_flags = set_flags;
			req.channel_modify.unset_mode_flags = unset_flags;
			req.peer->IncRef();
			req.callback = OnMode_FetchChannelInfo;
			scheduler->AddRequest(req.type, req);


			if (includeBanLookup) {
				req.type = EPeerchatRequestType_ListUserModes_CacheLookup;
				req.peer = this;
				req.usermodeRecord.chanmask = data_parser.at(1);
				req.peer->IncRef();
				req.callback = OnMode_FetchBanInfo;
				scheduler->AddRequest(req.type, req);
			}
		}
	}
	void Peer::handle_user_mode_command(std::vector<std::string> data_parser) {
		TaskScheduler<PeerchatBackendRequest, TaskThreadData>* scheduler = ((Peerchat::Server*)(GetDriver()->getServer()))->GetPeerchatTask();
		PeerchatBackendRequest req;
		if (data_parser.size() == 2) {
			//lookup
			req.type = EPeerchatRequestType_LookupUserDetailsByName;
			req.peer = this;
			req.summary.username = data_parser.at(1);
			req.peer->IncRef();
			req.callback = OnMode_FetchUserInfo;
			scheduler->AddRequest(req.type, req);
		}
		else if (data_parser.size() >= 3) { //set/unset
			// +m-n

			bool set = true;
			int set_flags = 0, unset_flags = 0;
			std::string mode_string = data_parser.at(2);
			for (size_t i = 0; i < mode_string.length(); i++) {
				if (mode_string[i] == '+') {
					set = true;
				}
				else if (mode_string[i] == '-') {
					set = false;
				}
				else {
					ModeFlagMap flag;
					bool found = false;
					for (int x = 0; x < num_user_mode_flags; x++) {
						if (user_mode_flag_map[x].character == mode_string[i]) {
							flag = user_mode_flag_map[x];
							found = true;
							break;
						}
					}
					if (found) {
						if (set) {
							set_flags |= flag.flag;
							unset_flags &= ~flag.flag;
						}
						else {
							unset_flags |= flag.flag;
							set_flags &= ~flag.flag;
						}
					}
				}
			}
			req.type = EPeerchatRequestType_UpdateUserModes;
			req.peer = this;
			req.summary = m_user_details;
			req.channel_summary.channel_name = data_parser.at(1);
			req.channel_modify.set_mode_flags = set_flags;
			req.channel_modify.unset_mode_flags = unset_flags;
			req.peer->IncRef();
			req.callback = OnMode_UpdateUserMode;
			scheduler->AddRequest(req.type, req);


			//unset quiet flags, resend channel names list
			if((unset_flags & EUserMode_Quiet) && !(set_flags & EUserMode_Quiet)) {
					mp_mutex->lock();
					std::map<int, int>::iterator it = m_channel_flags.begin();
					while (it != m_channel_flags.end()) {
						std::pair<int, int> p = *it;

						if(p.second & EUserChannelFlag_IsInChannel) {
							req.type = EPeerchatRequestType_LookupChannelDetails;
							req.peer = this;
							req.peer->IncRef();
							req.callback = OnNames_FetchChannelInfo;
							req.channel_summary.channel_id = p.first;
							scheduler->AddRequest(req.type, req);
						}
						
						it++;
					}
					mp_mutex->unlock();
			}
		}
	}
	void Peer::handle_mode(std::vector<std::string> data_parser) {
		std::string target;
		if (data_parser.size() >= 2) {
			target = data_parser.at(1);
		}
		if (target.length() > 0) {
			if (target[0] == '#') {
				handle_channel_mode_command(data_parser);
			}
			else {
				handle_user_mode_command(data_parser);
			}
		}
	}
}
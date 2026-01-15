"""Room container.

Room holds multiple User instances and provides simple operations to add,
remove and list users. This is deliberately small and synchronous; the
pilot_center server will call these methods from asyncio tasks so avoid
blocking operations here.
"""

from __future__ import annotations

import logging
import json
from typing import Dict, List, Optional
from .user import User
from .push_info import PushInfo
from .rtp_param import RtpParam

class Room:
	def __init__(self, room_id: str) -> None:
		"""Initialize a Room identified by room_id.

		Note: `name` field was removed — callers should use `room_id` as the
		canonical identifier if a human-friendly name is not available.
		"""
		self.room_id = room_id
		# per-room logger for easier filtering in logs
		self.log = logging.getLogger(f"room.{room_id}")
		# Log creation and constructor input parameters for observability
		self.log.info("Room created: room_id=%s, init_args=%s", room_id, {"room_id": room_id})
		# users keyed by user_id
		self._users: Dict[str, User] = {}

		# sessions keyed by session id (e.g. ip:port) for room-level broadcasts
		# value is the session object (WsProtooSession)
		self._sessions: Dict[str, object] = {}

	def add_user(self, user: User) -> bool:
		"""Add user to room. Return True if added, False if already present."""
		if user.user_id in self._users:
			return False
		self._users[user.user_id] = user
		return True

	def remove_user(self, user_id: str) -> bool:
		"""Remove user by id. Return True if removed, False if not found."""
		return self._users.pop(user_id, None) is not None

	def get_user(self, user_id: str) -> Optional[User]:
		return self._users.get(user_id)

	def list_users(self) -> List[User]:
		return list(self._users.values())

	def user_count(self) -> int:
		return len(self._users)

	def handle_join(self, user_id: str, user_name: str = "", audience: bool = False, session: object | None = None) -> dict:
		"""Handle a user joining this room.

		Creates a User if missing, associates the (single) session with the
		User and registers the session in the room's session map (keyed by
		session.peer if available). Returns True if the user was added, False
		if the user was already present.
		"""
		self.log.info("User %s(%s) joining room %s", user_id, user_name, self.room_id)
		existing = self.get_user(user_id)
		if existing is None:
			user = User(user_id=user_id, name=user_name)
			self.add_user(user)
		else:
			self.log.info("User %s already exists in room %s", user_id, self.room_id)
			user = existing

		if session is not None:
			# associate session with the user (User manages single session)
			try:
				user.add_session(session)
			except Exception:
				pass
			# register session under this room for broadcast lookups
			sid = getattr(session, "peer", None)
			if sid:
				try:
					self.add_session(sid, session)
				except Exception:
					pass
		# log the self.sessions size
		self.log.info("Room %s has %d registered sessions after user %s join", self.room_id, len(self._sessions), user_id)
		# call self.notify_new_user
		# notify other room members that a new user joined
		try:
			if not audience:
				self.notify_new_user(user_id, user_name, except_userId=user_id)
		except Exception:
			pass
		data = {
			"code": 0,
			"message": "join success",
			"roomId": self.room_id,
			"users": [],
		}
		for u in self.list_users():
			# skip the joining user
			if u.user_id == user_id:
				continue
			user_info = {
				"userId": u.user_id,
				"userName": u.name,
				"pushers": [],
			}
			pushers = u.get_pusher_info()
			for p in pushers.values():
				user_info["pushers"].append(p.to_dict())
			#skip pushers for now
			data["users"].append(user_info)

		return data

	def broadcast(self, method: str, payload: Optional[Dict] = None) -> None:
		"""Stub: broadcast a message to all users in the room.

		In this pilot_center prototype we don't have a real messaging channel
		(WebSocket sessions live in other modules). This method exists as a
		convenient hook — callers may iterate users and deliver messages.
		"""
		# Broadcast to all sessions registered in this room (best-effort).
		for sess in list(self._sessions.values()):
			try:
				# session should implement async send_notification; schedule fire-and-forget
				import asyncio as _asyncio
				_asyncio.create_task(sess.send_notification(method, payload))
			except Exception:
				# ignore individual session failures
				pass

	def broadcast_except_user(self, message: str, userId: str, method: str = "room.broadcast", payload: Optional[Dict] = None) -> None:
		"""Broadcast to all sessions in this room except the session belonging
		to `userId`.

		Backward compatible: when `payload` is None, this sends
		`{"message": message}` with method `method` (default "room.broadcast").
		If `payload` is provided, that dict is sent as the data for `method`.

		This uses the per-room session map (`_sessions`) and excludes the
		session returned by `User.get_session()` for `userId` (single-session
		model). The operation is best-effort (individual send failures are
		ignored).
		"""
		excluded_session = None
		user = self.get_user(userId)
		if user is not None:
			# determine the single session object to exclude (single-session model)
			try:
				excluded_session = user.get_session()
			except Exception:
				excluded_session = None

		data = payload if payload is not None else {"message": message}

		for sess in list(self._sessions.values()):
			# skip the excluded session (those belonging to the provided userId)
			if sess is excluded_session:
				continue
			try:
				import asyncio as _asyncio
				self.log.info("Broadcasting to session %s method=%s data=%s", getattr(sess, "peer", "unknown"), method, data)
				_asyncio.create_task(sess.send_notification(method, data))
			except Exception:
				# best-effort: ignore send failures
				self.log.info("Failed to send to session %s", getattr(sess, "peer", "unknown"))
				pass

	def add_session(self, session_id: str, session: object) -> None:
		"""Register a session under this room (for room-level broadcasts)."""
		if not session_id:
			return
		self._sessions[session_id] = session

	def remove_session(self, session_id: str) -> None:
		"""Remove a session previously registered for this room."""
		try:
			self._sessions.pop(session_id, None)
		except Exception:
			pass

	def notify_new_user(self, userId: str, name: str, except_userId: Optional[str] = None) -> None:
		"""Notify room members that a new user joined.

		Sends a notification with method `newUser` and data:
		{ "roomId": self.room_id, "userId": userId, "userName": name }

		If `except_userId` is provided, that user's session (if any) will be
		excluded from the broadcast.
		"""

		payload = {"roomId": self.room_id, "userId": userId, "userName": name}
		self.log.info("Notifying room %s members of new user %s(%s), except %s", self.room_id, userId, name, except_userId)
		# reuse broadcast_except_user to avoid duplicating the broadcast loop
		# pass the payload and method so it sends the correct notification shape
		# call broadcast_except_user(self, message: str, userId: str, method: str = "room.broadcast", payload: Optional[Dict] = None) -> None:
		self.broadcast_except_user("", except_userId or "", method="newUser", payload=payload)

	def handle_userDisconnect_notification(self, data: Dict[str, object], session: object) -> None:
		"""Handle a userDisconnect notification sent to this room.

		Removes the session from the room and disassociates it from the user.
		"""
		user_id = data.get("userId", "")
		self.log.info("Handling userDisconnect notification for user %s in room %s", user_id, self.room_id)
		user = self.get_user(user_id)
		if user is None:
			self.log.info("userDisconnect for unknown user %s in room %s", user_id, self.room_id)
			return

		self.log.info("userDisconnect completed for user %s in room %s", user_id, self.room_id)
		payload = {"roomId": self.room_id, "userId": user_id}
		self.broadcast_except_user("", user_id, method="userDisconnect", payload=payload)
		# remove session from user
		try:
			user.remove_session(session)
		except Exception:
			pass
		# donn't remove session from room for there may be other users using the same session
	def handle_userLeave_notification(self, data: Dict[str, object], session: object) -> None:
		"""Handle a userLeave notification sent to this room.

		Removes the user from the room and disassociates the session.
		"""
		user_id = data.get("userId", "")
		self.log.info("Handling userLeave notification for user %s in room %s", user_id, self.room_id)
		user = self.get_user(user_id)
		if user is None:
			self.log.info("userLeave for unknown user %s in room %s", user_id, self.room_id)
			return

		self.log.info("userLeave completed for user %s in room %s", user_id, self.room_id)
		payload = {"roomId": self.room_id, "userId": user_id}
		self.broadcast_except_user("", user_id, method="userLeave", payload=payload)
		# remove session from user
		try:
			user.remove_session(session)
		except Exception:
			pass
		# remove user from room
		try:
			self.remove_user(user_id)
		except Exception:
			pass

	def handle_textMessage_notification(self, data: Dict[str, object], session: object) -> None:
		"""Handle a textMessage notification sent to this room.

		Broadcasts the text message to all room members.
		"""
		user_id = data.get("userId", "")
		user_name = data.get("userName", "")
		message = data.get("message", "")
		self.log.info("Handling textMessage notification from user %s(%s) in room %s: %s", user_id, user_name, self.room_id, message)
		# broadcast the message to all room members
		self.broadcast_except_user("", user_id, method="textMessage", payload={
			"roomId": self.room_id,
			"userId": user_id,
			"userName": user_name,
			"message": message,
		})

	def handle_push_notification(self, data: Dict[str, object], session: object) -> None:
		"""Handle a push notification sent to this room.

		For now this is a stub that logs the data.
		"""
		pusher_user_id = data.get("userId", "")
		push_user_name = data.get("userName", "")
		publishers = data.get("publishers", [])
		self.log.info("Received push notification in room %s from user %s(%s) with publishers: %s",
			self.room_id, pusher_user_id, push_user_name, publishers)
		# Further processing can be implemented here as needed.

		user = self.get_user(pusher_user_id)
		if user is None:
			self.log.info("Push notification from unknown user %s in room %s", pusher_user_id, self.room_id)
			new_user = User(user_id=pusher_user_id, name=push_user_name)
			self.add_user(new_user)
			user = new_user
		push_user_name = user.get_name()
		# For now, we just log the pushers info.
		push_info_list = []
		for pub in publishers:
			rtpParam = pub.get("rtpParam", {})
			pusherId = pub.get("pusherId", "")
			push_info = PushInfo(pusherId=pusherId)
			push_info.set_rtp_param(RtpParam.from_dict(rtpParam))

			push_info_list.append(push_info)
			user.set_pusher_info(push_info)

		self.notify_new_pushers(pusher_user_id, push_user_name, push_info_list)

	def notify_new_pushers(self, userId: str, userName: str, push_info_list: List[PushInfo]) -> None:
		"""Notify room members that a user has new pushers.

		Sends a notification with method `newPusher` and data:
		{ "roomId": self.room_id, "userId": userId, "pushers": pushers }
		"""
		pushers_list = []
		for p in push_info_list:
			pushers_list.append({
				"pusherId": p.pusherId,
				"rtpParam": p.rtpParam.to_dict() if p.rtpParam else {},
			})
		
		payload = {"roomId": self.room_id, "userId": userId, "userName": userName, "pushers": pushers_list}
		self.broadcast_except_user("", userId, method="newPusher", payload=payload)

	def handle_pull_remote_stream_notification(self, data: Dict[str, object], session: object) -> None:
		"""Handle a pull remote stream notification sent to this room.

		For now this is a stub that logs the data.
		"""
		pusher_user_id = data.get("pusher_user_id", "")

		# send pull request to the pusher user in sfu
		pusher_user = self.get_user(pusher_user_id)
		if pusher_user is None:
			self.log.info("Pull remote stream notification for unknown pusher user %s in room %s", pusher_user_id, self.room_id)
			return
		session = pusher_user.get_session()
		if session is None:
			self.log.info("Pull remote stream notification for pusher user %s with no session in room %s", pusher_user_id, self.room_id)
			return
		import asyncio as _asyncio
		data_str = json.dumps(data)
		self.log.info("Sending pullRemoteStream notification to pusher user %s in room %s, data:%s", pusher_user_id, self.room_id, data_str)
		_asyncio.create_task(session.send_notification("pullRemoteStream", data))

	def handle_asr_result_notification(self, data: Dict[str, object], session: object) -> None:
		"""Handle an asr_result notification sent to this room.

		Broadcasts the asr result to all room members.
		"""

		"""
		{
		    "type": "asr_result",
		    "result": "I am a test ASR result",
		    "start_ms": "17000000",
		    "end_ms": "17111111",
		    "index": 1,
		    "ts": "1712344444",
		    "roomId": "abcde",
		    "userId": "eeeee",
		    "userName": "eeeee_username"
		}
		handle the asr_result notification sent to this room.
		"""
		user_id = data.get("userId", "")
		user_name = data.get("userName", "")
		asr_result = data.get("result", "")

		asr_str = user_name + ": " + asr_result

		self.log.info("Handling asr_result notification from user %s(%s) in room %s: %s", user_id, user_name, self.room_id, asr_result)
		# broadcast the asr result to all room members
		self.broadcast(method="textMessage", payload={
			"roomId": self.room_id,
			"userId": 'ai_asr_bot',
			"userName": 'AI_ASR_Bot',
			"message": asr_str,
		})

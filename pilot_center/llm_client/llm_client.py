import json
from typing import Any, Dict, List, Optional
from urllib import error, request


class LLMClient:
	"""HTTP client for posting messages to an LLM endpoint."""

	def __init__(
		self,
		url: str,
		token: str,
		model: str,
		tools: Optional[List[Dict[str, Any]]] = None,
		max_messages: int = 20,
	) -> None:
		self.url = url
		self.token = token
		self.model = model
		self.tools: List[Dict[str, Any]] = tools or []
		self.messages: List[Dict[str, Any]] = []
		self.max_messages = max_messages

	def PostMessage(self, message: str, timeout: int = 30) -> Dict[str, Any]:
		"""
		Send a message to the LLM using the OpenAI-style chat/completions format.

		Returns the parsed JSON response or a structured error payload.
		"""

		user_message = {"role": "user", "content": message}
		self.messages.append(user_message)

		if self.max_messages > 0 and len(self.messages) > self.max_messages:
			# Drop oldest messages to enforce the cap.
			overflow = len(self.messages) - self.max_messages
			if overflow > 0:
				self.messages = self.messages[overflow:]

		payload: Dict[str, Any] = {
			"model": self.model,
			"messages": self.messages,
		}

		if self.tools:
			payload["tools"] = self.tools

		data = json.dumps(payload).encode("utf-8")
		headers = {
			"Content-Type": "application/json",
			"Accept": "application/json",
			"Authorization": f"Bearer {self.token}",
		}

		req = request.Request(self.url, data=data, headers=headers, method="POST")

		try:
			with request.urlopen(req, timeout=timeout) as resp:
				resp_body = resp.read().decode("utf-8")
				if resp_body:
					try:
						resp_data = json.loads(resp_body)
					except ValueError:
						return {"status": resp.status, "reason": "invalid_json", "body": resp_body}

					# Expected OpenAI chat response shape:
					# {
					#   "id": "...",
					#   "choices": [
					#     {"index": 0, "message": {"role": "assistant", "content": "...", "tool_calls": [...]}, "finish_reason": "stop"}
					#   ],
					#   "usage": {...}
					# }
					choices = resp_data.get("choices") or []
					if choices and isinstance(choices[0], dict):
						message_obj = choices[0].get("message")
						if message_obj is not None and message_obj.get("role") == "assistant":
							return message_obj

					return resp_data
				return {"status": resp.status, "reason": resp.reason, "body": ""}
		except error.HTTPError as http_err:
			body = http_err.read().decode("utf-8") if http_err.fp else ""
			return {
				"status": http_err.code,
				"reason": http_err.reason,
				"body": body,
			}
		except error.URLError as url_err:
			return {"status": "network_error", "reason": str(url_err)}


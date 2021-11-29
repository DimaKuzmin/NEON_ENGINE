#pragma once
class xrVoice
{
	xr_vector<NET_Packet*> captured;

	xrVoice();
	~xrVoice();

	void InitCapture();

	void capture();

};


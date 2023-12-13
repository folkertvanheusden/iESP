class server
{
private:
	int listen_fd { -1 };

public:
	server();
	virtual ~server();

	bool begin();

	void handler();
};

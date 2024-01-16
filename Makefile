CC = g++
OPENCV =  `pkg-config --cflags --libs opencv`
PTHREAD = -pthread

RECEIVER = receiver.cpp
SENDER = sender.cpp
AGENT = agent.cpp
REC = receiver
SEN = sender
AGE = agent

all: sender receiver agent
  
sender: $(SENDER)
	$(CC) $(SENDER) -o $(SEN)  $(OPENCV) $(PTHREAD) 
receiver: $(RECEIVER)
	$(CC) $(RECEIVER) -o $(REC)  $(OPENCV) $(PTHREAD) -g
agent: $(AGENT)
	$(CC) $(AGENT) -o $(AGE)  $(OPENCV) $(PTHREAD)

.PHONY: clean

clean:
	rm $(REC) $(SEN) $(AGE)
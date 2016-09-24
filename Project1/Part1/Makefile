CXX = g++

CFLAGS = -g -std=c++11 `pkg-config --cflags libvirt`
LDFLAGS= `pkg-config --libs libvirt`

TARGET = vcpu_sched
TARGET_CXX = cpu_scheduler.cpp 

$(TARGET):
	$(CXX) $(CFLAGS) -o $(TARGET) $(TARGET_CXX) $(LDFLAGS)

clean:
	$(RM) $(TARGET)

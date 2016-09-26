CXX = g++

CFLAGS = -g -std=c++11 `pkg-config --cflags libvirt`
LDFLAGS= `pkg-config --libs libvirt`

TARGET = vmem_coord
TARGET_CXX = mem_coordinator.cpp 

$(TARGET):
	$(CXX) $(CFLAGS) -o $(TARGET) $(TARGET_CXX) $(LDFLAGS)

clean:
	$(RM) $(TARGET)

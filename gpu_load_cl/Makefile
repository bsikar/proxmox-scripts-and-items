LOAD_CL_TARGET = gpu_load_cl
LOAD_CL_SRC = gpu_load_cl.cpp
CL_LIBS = -lOpenCL

# In your 'all' target, add $(LOAD_CL_TARGET)
all: $(MONITOR_TARGET) $(LOAD_CL_TARGET)

$(LOAD_CL_TARGET): $(LOAD_CL_SRC)
        $(CXX) $(CXXFLAGS) $(LOAD_CL_SRC) -o $(LOAD_CL_TARGET) $(CL_LIBS)

# In your 'clean' target, add $(LOAD_CL_TARGET)
clean:
        rm -f $(MONITOR_TARGET) $(LOAD_TARGET) $(LOAD_CL_TARGET)
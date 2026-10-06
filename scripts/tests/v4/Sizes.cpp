#include "Core.hpp"
#include "Worker.hpp"
// Uncalled static compiler probes, not a platform integration or firmware image.
extern "C" {
char pqv4_size_core[sizeof(ProtogenV4::Core)];
char pqv4_size_worker[sizeof(ProtogenV4::SlotWorker)];
char pqv4_size_result[sizeof(ProtogenV4::Result)];
char pqv4_size_material[sizeof(ProtogenV4::Material)];
char pqv4_size_exchange[sizeof(ProtogenV4::Exchange)];
char pqv4_size_reassembly[sizeof(ProtogenV4::Reassembly)];
}

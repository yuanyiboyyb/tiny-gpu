import cocotb
from cocotb.clock import Clock
from cocotb.triggers import FallingEdge, ReadOnly, RisingEdge, Timer


MATADD_PROGRAM = [
    0b0101000011011110,  # MUL   R0, %blockIdx, %blockDim
    0b0011000000001111,  # ADD   R0, R0, %threadIdx      ; i = blockIdx * blockDim + threadIdx
    0b1001000100000000,  # CONST R1, #0                 ; baseA
    0b1001001000001000,  # CONST R2, #8                 ; baseB
    0b1001001100010000,  # CONST R3, #16                ; baseC
    0b0011010000010000,  # ADD   R4, R1, R0             ; address of A[i]
    0b0111010001000000,  # LDR   R4, R4                 ; load A[i]
    0b0011010100100000,  # ADD   R5, R2, R0             ; address of B[i]
    0b0111010101010000,  # LDR   R5, R5                 ; load B[i]
    0b0011011001000101,  # ADD   R6, R4, R5             ; C[i] = A[i] + B[i]
    0b0011011100110000,  # ADD   R7, R3, R0             ; address of C[i]
    0b1000000001110110,  # STR   R7, R6                 ; store C[i]
    0b1111000000000000,  # RET
] 



MATMUL_PROGRAM = [
    0b0101000011011110,  # MUL   R0, %blockIdx, %blockDim
    0b0011000000001111,  # ADD   R0, R0, %threadIdx      ; i = blockIdx * blockDim + threadIdx
    0b1001000100000001,  # CONST R1, #1                 ; increment
    0b1001001000000010,  # CONST R2, #2                 ; matrix dimension N
    0b1001001100000000,  # CONST R3, #0                 ; baseA
    0b1001010000000100,  # CONST R4, #4                 ; baseB
    0b1001010100001000,  # CONST R5, #8                 ; baseC
    0b0110011000000010,  # DIV   R6, R0, R2             ; row = i / N
    0b0101011101100010,  # MUL   R7, R6, R2
    0b0100011100000111,  # SUB   R7, R0, R7             ; column = i % N
    0b1001100000000000,  # CONST R8, #0                 ; accumulator = 0
    0b1001100100000000,  # CONST R9, #0                 ; k = 0
                            # LOOP:
    0b0101101001100010,  # MUL   R10, R6, R2
    0b0011101010101001,  # ADD   R10, R10, R9
    0b0011101010100011,  # ADD   R10, R10, R3           ; address of A[row][k]
    0b0111101010100000,  # LDR   R10, R10               ; load A[row][k]
    0b0101101110010010,  # MUL   R11, R9, R2
    0b0011101110110111,  # ADD   R11, R11, R7
    0b0011101110110100,  # ADD   R11, R11, R4           ; address of B[k][column]
    0b0111101110110000,  # LDR   R11, R11               ; load B[k][column]
    0b0101110010101011,  # MUL   R12, R10, R11
    0b0011100010001100,  # ADD   R8, R8, R12            ; accumulator += A * B
    0b0011100110010001,  # ADD   R9, R9, R1             ; k++
    0b0010000010010010,  # CMP   R9, R2
    0b0001100000011000,  # BRn   #24                    ; loop while k < N
    0b0011100101010000,  # ADD   R9, R5, R0             ; address of C[i]
    0b1000000010011000,  # STR   R9, R8                 ; store C[i]
    0b1111000000000000,  # RET
]


class Memory:
    """One-cycle ready/valid memory model for the GPU top-level ports."""

    def __init__(self, dut, name, addr_bits, data_bits, channels):
        if data_bits % 8 != 0:
            raise ValueError(f"{name} memory width must be a whole number of bytes")

        # The backing store is always byte-addressed. A transfer may combine
        # multiple consecutive bytes (two bytes for a 16-bit instruction).
        self.memory = [0] * (1 << addr_bits)
        self.channels = channels
        self.name = name
        self.addr_bits = addr_bits
        self.data_bits = data_bits
        self.bytes_per_word = (data_bits + 7) // 8
        self.data_mask = (1 << data_bits) - 1
        self.read_valid = getattr(dut, f"{name}_mem_read_valid")
        self.read_address = getattr(dut, f"{name}_mem_read_address")
        self.read_ready_port = getattr(dut, f"{name}_mem_read_ready")
        self.read_data = getattr(dut, f"{name}_mem_read_data")
        self.read_ready = [0] * channels

        if name == "data":
            self.write_valid = dut.data_mem_write_valid
            self.write_address = dut.data_mem_write_address
            self.write_data = dut.data_mem_write_data
            self.write_ready_port = dut.data_mem_write_ready
            self.write_ready = [0] * channels

        self.read_ready_port.value = 0
        for channel in range(channels):
            self.read_data[channel].value = 0
        if name == "data":
            self.write_ready_port.value = 0

    @staticmethod
    def _pack_ready(channel_ready):
        """Pack one ready value per channel into the RTL ready vector."""
        packed_ready = 0
        for channel, ready in enumerate(channel_ready):
            packed_ready |= (int(bool(ready)) << channel)
        return packed_ready

    def load(self, values):
        for word_address, value in enumerate(values):
            byte_address = word_address * self.bytes_per_word
            if byte_address + self.bytes_per_word > len(self.memory):
                raise ValueError(f"{self.name} memory image is too large")
            value = int(value) & self.data_mask
            for byte_offset in range(self.bytes_per_word):
                self.memory[byte_address + byte_offset] = (
                    value >> (8 * byte_offset)
                ) & 0xFF

    def _read_word(self, byte_address):
        if byte_address + self.bytes_per_word > len(self.memory):
            raise ValueError(
                f"{self.name} read at {byte_address} crosses memory boundary"
            )

        value = 0
        for byte_offset in range(self.bytes_per_word):
            value |= (
                self.memory[byte_address + byte_offset] << (8 * byte_offset)
            )
        return value & self.data_mask

    def _write_word(self, byte_address, value):
        if byte_address + self.bytes_per_word > len(self.memory):
            raise ValueError(
                f"{self.name} write at {byte_address} crosses memory boundary"
            )

        value = int(value) & self.data_mask
        for byte_offset in range(self.bytes_per_word):
            self.memory[byte_address + byte_offset] = (
                value >> (8 * byte_offset)
            ) & 0xFF

    def drive(self):
        read_valid = int(self.read_valid.value)
        for channel in range(self.channels):
            self.read_ready[channel] = 0
            if (read_valid >> channel) & 1:
                address = int(self.read_address[channel].value)
                self.read_data[channel].value = self._read_word(address)
                self.read_ready[channel] = 1
            else:
                self.read_data[channel].value = 0
        self.read_ready_port.value = self._pack_ready(self.read_ready)

        if self.name == "data":
            write_valid = int(self.write_valid.value)
            for channel in range(self.channels):
                self.write_ready[channel] = 0
                if (write_valid >> channel) & 1:
                    address = int(self.write_address[channel].value)
                    value = int(self.write_data[channel].value) & self.data_mask
                    self._write_word(address, value)
                    self.write_ready[channel] = 1
            self.write_ready_port.value = self._pack_ready(self.write_ready)


async def setup_gpu(dut, program, data, threads):
    program_memory = Memory(dut, "program", 8, 16, 1)
    data_memory = Memory(dut, "data", 8, 8, 4)
    program_memory.load(program)
    data_memory.load(data)

    dut.reset.value = 1
    dut.start.value = 0
    dut.device_control_write_enable.value = 0
    dut.device_control_data.value = 0

    cocotb.start_soon(Clock(dut.clk, 10, units="ns").start())
    await RisingEdge(dut.clk)
    await RisingEdge(dut.clk)

    # Change synchronous inputs on falling edges so they remain stable around
    # the following rising edge where the RTL samples them.
    await FallingEdge(dut.clk)
    dut.reset.value = 0
    dut.device_control_write_enable.value = 1
    dut.device_control_data.value = threads
    await RisingEdge(dut.clk)

    await FallingEdge(dut.clk)
    dut.device_control_write_enable.value = 0
    dut.start.value = 1
    await RisingEdge(dut.clk)

    return program_memory, data_memory


async def run_until_done(dut, program_memory, data_memory, max_cycles=2000):
    for cycle in range(max_cycles):
        # Allow registered outputs to settle, then supply memory responses before
        # the next active edge.
        await Timer(1, units="ns")
        program_memory.drive()
        data_memory.drive()
        await RisingEdge(dut.clk)
        await ReadOnly()
        if int(dut.done.value):
            dut._log.info("GPU completed in %d cycles", cycle + 1)
            await FallingEdge(dut.clk)
            dut.start.value = 0
            return
    raise AssertionError(
        f"GPU did not assert done within {max_cycles} cycles; inspect dump.vcd"
    )


@cocotb.test()
async def test_matadd(dut):
    data = list(range(1, 9)) + list(range(1, 9))
    program_memory, data_memory = await setup_gpu(
        dut, MATADD_PROGRAM, data, threads=8
    )
    await run_until_done(dut, program_memory, data_memory)

    expected = [value * 2 for value in range(1, 9)]
    actual = data_memory.memory[16:24]
    assert actual == expected, f"matadd expected {expected}, got {actual}"


@cocotb.test()
async def test_matmul(dut):
    data = [1, 2, 3, 4, 1, 2, 3, 4]
    program_memory, data_memory = await setup_gpu(
        dut, MATMUL_PROGRAM, data, threads=4
    )
    await run_until_done(dut, program_memory, data_memory)

    expected = [7, 10, 15, 22]
    actual = data_memory.memory[8:12]
    assert actual == expected, f"matmul expected {expected}, got {actual}"

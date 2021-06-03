import com.fazecast.jSerialComm.SerialPort
import com.fazecast.jSerialComm.SerialPortDataListener
import com.fazecast.jSerialComm.SerialPortEvent
import java.util.*
import kotlin.io.path.*

fun main(args: Array<String>) {

    val iface = ProbeToolInterface(SerialPort.getCommPorts().first { it.systemPortName == "ttyUSB0" })

    fun startProbe(): Int {
        iface.sendCommand("g")
        val startWaiting = System.currentTimeMillis()
        while (System.currentTimeMillis() - startWaiting < 10000) {
            val l = iface.readLine()
            if (l != null) {
                if (l.startsWith("ERR")) throw Exception("Error while probing.")
                if (l.startsWith("PROBE "))
                    return l.removePrefix("PROBE ").toInt()
            }
            Thread.sleep(50)
        }
        iface.sendCommand("s")
        throw Exception("Probing timed out.")
    }

    fun pos() = iface.sendCommand("p").value!!
    fun rpm(rpm: Int) = iface.sendCommand("r", rpm)

    fun move(loc: Int) {
        iface.sendCommand("m", loc)
        while (pos() != loc) {
            Thread.sleep(50)
        }
    }

    fun home() {
        iface.sendCommand("h")
    }

    fun readProbe() = iface.sendCommand("a").value!!
    fun threshMin(thresh: Int) = iface.sendCommand("tmin", thresh)
    fun threshMax(thresh: Int) = iface.sendCommand("tmax", thresh)

    println("Pin number:")
    val userPin = readLine()!!.toInt()
    println("Threshold: ")
    val userThresh = readLine()!!.toInt()
    println("Repeat: ")
    val userRepeat = readLine()!!.toInt()

    if (userThresh < 300 || userThresh > 900) throw Exception()
    if (userRepeat < 1 || userRepeat > 100) throw Exception()

    val testName = "pin${userPin}_thresh${userThresh}.txt"

    val file = Path(System.getProperty("user.home")) / "probe" / testName
    file.deleteIfExists()

    val writer = file.bufferedWriter()

    println("Move fuckhead!")
    Thread.sleep(10000)

    val retract = 800
    try {
        if (readProbe() > userThresh) {
            println("Position probe close above water!")
            return
        }

        startProbe()
        home()
        threshMax(userThresh)

        val results = mutableListOf<Double>()

        repeat(userRepeat) {
            print("Pass ${it + 1}/$userRepeat ")
            move(-retract)
            Thread.sleep(1000)
            val probeValue =
                if (readProbe() < 100) -1
                else startProbe()
            println(probeValue)
            writer.appendLine(probeValue.toString())
            writer.flush()
            results += probeValue.toDouble()
        }

        val avg = results.average()
        val max = results.maxOrNull()!! - avg
        val min = results.minOrNull()!! - avg
        val err = max - min
        println("min: $min, max: $max, err: $err")
        writer.appendLine("# min: $min, max: $max, err: $err")
        // Reset
        move(retract)



    } finally {
        writer.close()
        iface.close()
    }

}

class ProbeToolInterface(
    private val port: SerialPort,
    private val printLog: Boolean = false
) : SerialPortDataListener {

    private val readBytes = LinkedList<Byte>()
    private val readyLines = LinkedList<String>()

    private val lock = Any()

    init {
        port.baudRate = 115200
        port.setComPortTimeouts(SerialPort.TIMEOUT_NONBLOCKING, 0, 0)
        if (!port.openPort()) {
            throw Exception("Cannot open port")
        }
        port.addDataListener(this)
    }

    fun sendCommand(cmd: String, vararg args: Int): CommandResult {
        val bytes = buildString {
            append(cmd).append(" ")
            args.forEachIndexed { index, arg ->
                append(arg)
                if (index != args.lastIndex) append(" ")
            }
            append("\n\r")
        }.toByteArray(Charsets.US_ASCII)

        if (printLog)
            println("=> '${String(bytes.filter { !isEOL(it) }.toByteArray(), Charsets.US_ASCII)}'")

        port.writeBytes(bytes, bytes.size.toLong())

        // Read lines until 'OK Code' or 'ERR'
        var returnValue: Int? = null
        while (true) {
            val l = readLine() ?: throw Exception("Timeout")
            if (l.startsWith("ERR")) throw Exception("Device returned error: $l")
            if (l.startsWith("OK Code ")) {
                return CommandResult(l.removePrefix("OK Code ").toInt(), returnValue).also {
                    if (printLog)
                        println("$$cmd ${args.joinToString().trim()} -> ${it.code} [${it.value}]")
                }
            }
            if (l.startsWith("RET ")) {
                returnValue = l.removePrefix("RET ").toInt()
            }
        }
    }

    fun close() {
        port.closePort()
    }

    fun readLine(): String? {
        val startTime = System.currentTimeMillis()
        while (true) {
            val line = synchronized(lock) { readyLines.pollFirst() }
            if (line != null)
                return line
            if (System.currentTimeMillis() - startTime > 5000)
                return null
            Thread.sleep(50)
        }
    }

    override fun getListeningEvents(): Int = SerialPort.LISTENING_EVENT_DATA_AVAILABLE

    override fun serialEvent(event: SerialPortEvent?) {
        if (event?.eventType != SerialPort.LISTENING_EVENT_DATA_AVAILABLE) return
        synchronized(lock) {
            val newBytes = ByteArray(port.bytesAvailable())
            port.readBytes(newBytes, newBytes.size.toLong())
            newBytes.forEach { readBytes.addLast(it) }

            while (true) {
                // Drop eol chars at start.
                while (readBytes.isNotEmpty() && isEOL(readBytes.first)) readBytes.removeFirst()

                // Check if we got a line.
                val endOfLine = readBytes.indexOfFirst { isEOL(it) }.takeIf { it != -1 } ?: return
                val line = String(readBytes.slice(0 until endOfLine).toByteArray(), Charsets.US_ASCII).trim()
                if (line.isNotEmpty()) {
                    readyLines.addLast(line)
                    // Report errors
                    if (line.startsWith("ERR"))
                        System.err.println("<= '$line'")
                    else if (printLog)
                        println("<= '$line'")
                }

                // Drop processed bytes.
                repeat(endOfLine) { readBytes.removeFirst() }
            }
        }
    }

    companion object {
        private fun isEOL(b: Byte) = (b == 13.toByte() || b == 10.toByte())
    }
}

data class CommandResult(val code: Int, val value: Int?)
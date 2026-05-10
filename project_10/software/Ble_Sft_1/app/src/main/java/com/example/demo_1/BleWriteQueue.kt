package com.example.demo_1

import android.util.Log

internal class WriteFrame(
    val serviceUuid: String,
    val characteristicUuid: String,
    val data: ByteArray
)

internal class BleWriteQueue(
    private val doWrite: (WriteFrame) -> Boolean
) {
    companion object {
        private const val TAG = "BleWriteQueue"
        private const val MAX_QUEUE_SIZE = 32
    }

    private val queue = ArrayDeque<WriteFrame>()
    private var pending = false
    private var pendingFrame: WriteFrame? = null
    private var retryCount = 0
    private val lock = Any()

    fun enqueue(serviceUuid: String, characteristicUuid: String, data: ByteArray) {
        synchronized(lock) {
            if (queue.size >= MAX_QUEUE_SIZE) {
                val dropped = queue.removeFirstOrNull()
                if (dropped != null) {
                    val cmd = if (dropped.data.size > 2) (dropped.data[2].toInt() and 0xFF) else -1
                    Log.w(TAG, "Queue full, dropping oldest frame cmd=0x${"%02X".format(cmd)}")
                }
            }
            queue.addLast(WriteFrame(serviceUuid, characteristicUuid, data))
            if (!pending) tryWriteNext()
        }
    }

    fun onWriteCompleted(status: Int) {
        synchronized(lock) {
            val frame = pendingFrame
            if (status == 0) {
                if (retryCount > 0 && frame != null) {
                    val cmd = if (frame.data.size > 2) (frame.data[2].toInt() and 0xFF) else -1
                    Log.d(TAG, "Retry succeeded cmd=0x${"%02X".format(cmd)} after $retryCount attempt(s)")
                }
                retryCount = 0
                pending = false
                pendingFrame = null
                tryWriteNext()
            } else {
                if (frame != null) {
                    val cmd = if (frame.data.size > 2) (frame.data[2].toInt() and 0xFF) else -1
                    val cnt = if (frame.data.size > 14) (frame.data[14].toInt() and 0xFF) else -1
                    retryCount++
                    Log.w(TAG, "Write failed status=$status cmd=0x${"%02X".format(cmd)} cnt=0x${"%02X".format(cnt)}, retry attempt $retryCount")
                    pending = false
                    pendingFrame = null
                    // Re-insert at front so this frame is retried before any queued frames
                    queue.addFirst(WriteFrame(frame.serviceUuid, frame.characteristicUuid, frame.data))
                } else {
                    pending = false
                }
                tryWriteNext()
            }
        }
    }

    fun clear() {
        synchronized(lock) {
            queue.clear()
            pending = false
            pendingFrame = null
            retryCount = 0
        }
    }

    // Called within synchronized(lock)
    private fun tryWriteNext() {
        val frame = queue.removeFirstOrNull() ?: return
        val cmd = if (frame.data.size > 2) (frame.data[2].toInt() and 0xFF) else -1
        pendingFrame = frame
        pending = true
        val success = doWrite(frame)
        if (!success) {
            Log.w(TAG, "writeCharacteristic returned false cmd=0x${"%02X".format(cmd)}, dropping frame")
            pending = false
            pendingFrame = null
            tryWriteNext()
        }
    }
}

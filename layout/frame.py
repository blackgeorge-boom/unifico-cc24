import gdb
import platform


BORDER_LIMIT = 76
STACK_BORDER = BORDER_LIMIT * '+'
FRAME_BORDER = BORDER_LIMIT * '='


# Now create a gdb command that prints the current stack:
class PrintFrame(gdb.Command):
    """
    Display the stack memory layout for all frames.
    """
    def __init__ (self):
        super(PrintFrame, self).__init__ ("frame_info", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        try:
            print(STACK_BORDER)

            frame = gdb.newest_frame()
            while frame is not None:

                if platform.machine() == 'x86_64':
                    stack_pointer = frame.read_register('rsp')
                    base_pointer = frame.read_register('rbp')
                elif platform.machine() == 'aarch64':
                    stack_pointer = frame.read_register('sp')
                    base_pointer = frame.read_register('x29')
                else:
                    print('Unsupported architecture.')
                    exit()

                addr_diff = int(base_pointer) - int(stack_pointer) + 16
                words =  addr_diff / 4

                x_cmd = 'x/{}x {}'.format(int(words), stack_pointer)
                gdb.execute(x_cmd)

                frame = frame.older()
                print(FRAME_BORDER)

        except gdb.error:
            print("gdb got an error. Maybe we are not currently running?")

PrintFrame()
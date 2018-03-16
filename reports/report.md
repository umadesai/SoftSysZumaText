# ZumaText Project Report

Uma Desai & Zhengyang Feng

## Goal
The goal of this project is to build our own text editor that effectively implements all of the basic features expected of a minimal editor. Our stretch goal is to add more advanced features that build off of the basic Kilo tutorial. 

## Learning Goals

We want to:

- Learn new C tools and get more comfortable using the language under UNIX environment.

- Learn how to build a text editor from scratch using system calls and other functions.

- Learn how to communicate with the program using a keyboard, mouse, and shortcuts like the emacs editor.

- Learn more about advanced C features in UNIX, such as the terminal environment and argument processing.

- Learn how to organize a large project and implement simple OOP, especially with highly interdependent functions.

- Learn how to extend features from large, existing projects.


## Resources

The primary resource we used for getting started on the foundation of the project is the Kilo Snaptoken tutorial: https://viewsourcecode.org/snaptoken/kilo/. We also frequently referenced StackOverflow and the following sites for more context on implementing text editors in C:

- http://cs241.cs.illinois.edu/text_editor.html

- http://www.drdobbs.com/architecture-and-design/text-editors-algorithms-and-architecture/184408975

## Outcomes

We were successful in building a text editor with a vim-like interface and emacs-like shortcuts. We implemented each of the following capabilities in our text editor:

- User interface

![User interface](https://github.com/umadesai/SoftSysZumaText/blob/master/reports/interface.png)

- Input and output

- Status bar 

- Open, new, and save files

- Search

- Keyword and comment highlight

- Command shortcuts

**Try it for yourself!** To run our text editor, clone our repository, navigate to the editor folder, and run the following commands in the Unix command line:

make

./zuma [filename]


## Reflection

We were able to meet our stretch goal of implementing all the basic features of a minimal text editor, as well as a few custom, more advanced features! During this process, we were not only able to develop a fully functioning text editor, but also learn many new C tools by using UNIX libraries like termios, fcntl, errno, and ioctl. We were also able to practice implementing concepts we’ve been learning in class, such as using Makefiles, structs, enums, pointers, and dynamic memory allocation.

## Audience

The implemetation of our text editor consists of a large number of low-level functions. They are focused on an audience of UNIX users. What's great is that the readers of this project or the Kilo tutorial should be able to easily fork the repo and add some of their own extensions, like custom shortcuts, colors, or line numbers. 


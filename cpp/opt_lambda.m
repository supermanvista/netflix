x = [.001875 .00375 .0075 .015 .03 .044 .05 .075];
y = [.979892 .97393 .955205 .940164 .921561 .917351 .917677 .945704];
%%
t=linspace(0,max(x)*1.2);
plot(x,y,'o',t,polyval(polyfit(x,y,2),t));
%%
data = [0.042308 0.917279
0.042471 0.914645
0.042519 0.916314
0.042549 0.914041
0.042596 0.916263
0.042674 0.916328];
x = data(:,1); y = data(:,2);

plot(x,y,'o')
